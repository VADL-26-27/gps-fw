#![no_std]
#![no_main]

use cortex_m_rt::entry;
use cortex_m_semihosting::{hprint, hprintln};
use panic_semihosting as _;

use stm32f4xx_hal::{
    dma::{MemoryToPeripheral, StreamsTuple, Transfer}, gpio::alt::sdio, pac, prelude::*, rcc::Config, sdio::{ClockFreq, SdCard, Sdio}
};

use stm32f4xx_hal::dma;
use cortex_m::interrupt::Mutex;
use core::cell::RefCell;

// sd buffer size
const SD_BUFFER_SIZE: usize = 128*32;

// Simple ring buffer
pub struct Buffer {
    buffer: [u8; SD_BUFFER_SIZE],
    write_idx: usize,
    read_idx: usize,
}

impl Buffer {
    pub(crate) const fn new() -> Buffer {
        Buffer {
            buffer: [0; SD_BUFFER_SIZE],
            write_idx: 0,
            read_idx: 0,
        }
    }

    pub fn push(&mut self, data: u8) {
        self.buffer[self.write_idx] = data;
        self.write_idx = (self.write_idx + 1) % SD_BUFFER_SIZE;
    }

    pub fn read(&mut self) -> Option<u8> {
        if self.write_idx != self.read_idx {
            let data = self.buffer[self.read_idx];
            self.read_idx = (self.read_idx + 1) % SD_BUFFER_SIZE;
            Some(data)
        } else {
            None
        }
    }
}

// dma transfer type
// REF: rm0390 pg. 204, Table 29
type SdioDma = dma::Transfer<
    dma::Stream3<pac::DMA2>,
    4,
    pac::SDIO,
    MemoryToPeripheral,
    &'static mut [u8; SD_BUFFER_SIZE],
>;

// shared dma transfer reference
pub static G_TRANSFER: Mutex<RefCell<Option<SdioDma>>> = Mutex::new(RefCell::new(None));

// shared memory buffer reference
pub static G_SD_BUFFER: Mutex<RefCell<Option<Buffer>>> = Mutex::new(RefCell::new(None));

use static_cell::ConstStaticCell;

// dma buffer
pub static SD_BUFFER: ConstStaticCell<[u8; SD_BUFFER_SIZE]> =
    ConstStaticCell::new([0; SD_BUFFER_SIZE]);

// a wrapper function that reads out of the uart ring buffer
pub fn log_read_until(eol: u8) -> Option<[u8; SD_BUFFER_SIZE]> {
    let r = cortex_m::interrupt::free(|cs| {
        if let Some(buffer) = G_SD_BUFFER.borrow(cs).borrow_mut().as_mut() {
            let mut buf = [0; SD_BUFFER_SIZE];
            let mut i = 0;
            while let Some(byte) = buffer.read() {
                if byte == eol {
                    break;
                }
                if i < SD_BUFFER_SIZE - 1 {
                    buf[i] = byte;
                } else {
                    break;
                }
                i += 1;
            }
            if buf[0] == 0 {
                return None;
            }
            Some(buf)
        } else {
            None
        }
    });
    r
}

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::Peripherals::take().unwrap();

    let mut rcc = dp.RCC.freeze(
        Config::hse(12.MHz())
            .require_pll48clk()
            .sysclk(168.MHz())
            .hclk(168.MHz())
            .pclk1(42.MHz())
            .pclk2(84.MHz()),
    );

    assert!(rcc.clocks.is_pll48clk_valid());

    let mut delay = cp.SYST.delay(&rcc.clocks);

    let gpioc = dp.GPIOC.split(&mut rcc);
    let gpiod = dp.GPIOD.split(&mut rcc);

    // gives a tuple of all streams on DMA2 and starts clock
    let dma2 = StreamsTuple::new(dp.DMA2, &mut rcc);

    let d0 = gpioc.pc8.internal_pull_up(true);
    let d1 = gpioc.pc9.internal_pull_up(true);
    let d2 = gpioc.pc10.internal_pull_up(true);
    let d3 = gpioc.pc11.internal_pull_up(true);
    let clk = gpioc.pc12;
    let cmd = gpiod.pd2.internal_pull_up(true);

    let mut sdio: Sdio<SdCard> = Sdio::new(dp.SDIO, (clk, cmd, d0, d1, d2, d3), &mut rcc);

    hprintln!("Waiting for card...");

    // Wait for card to be ready
    loop {
        match sdio.init(ClockFreq::F24Mhz) {
            Ok(_) => break,
            Err(_err) => (),
        }

        delay.delay_ms(1000);
    }

    let sd_buffer = cortex_m::singleton!(: [u8; SD_BUFFER_SIZE] = [0; SD_BUFFER_SIZE]).unwrap();

    let mut sd_transfer = Transfer::init_memory_to_peripheral(
        dma2.3,
        sdio,
        sd_buffer,
        None,
        dma::config::DmaConfig::default()
            .memory_increment(true)
            .fifo_enable(true)
            .fifo_error_interrupt(true)
            .transfer_complete_interrupt(true),
    );

    let nblocks = sdio.card().map(|c| c.block_count()).unwrap_or(0);
    hprintln!("Card detected: nbr of blocks: {:?}", nblocks);

    // Read a block from the card and print the data
    let mut block = [0u8; 512];

    match sdio.read_block(0, &mut block) {
        Ok(()) => (),
        Err(err) => {
            hprintln!("Failed to read block: {:?}", err);
        }
    }

    for b in block.iter() {
        hprint!("{:X} ", b);
    }

    loop {
        continue;
    }
}
