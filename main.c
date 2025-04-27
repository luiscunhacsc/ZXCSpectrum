#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "z80.h"

#define SDL_MAIN_HANDLED  // Prevent SDL redefining main
#include <SDL2/SDL.h>

#define ROM_SIZE         0x4000        // 16 KB ROM
#define SCREEN_W         256
#define SCREEN_H         192
#define CYCLES_PER_FRAME (3500000/50)  // ≈3.5 MHz / 50 Hz

// memória: ROM + RAM
static uint8_t memory[65536];
// cor da border
static uint8_t border_color = 0;

// teclado: 8 filas × 5 bits (1 = não pressionado, 0 = pressionado)
static uint8_t key_matrix[8];

// flash do atributo (cursor pisca)
static int   flash_counter = 0;
static bool  flash_state   = false;

// inicializa matrix do teclado
static void init_keyboard() {
    for (int i = 0; i < 8; i++)
        key_matrix[i] = 0x1F;
}

// atualiza um bit na matrix (pressed→0, released→1)
static void update_key(int row, int bit, bool pressed) {
    if (pressed)  key_matrix[row] &= ~(1 << bit);
    else          key_matrix[row] |=  (1 << bit);
}

// mapeia SDL_Scancode → line/bit, incluindo Symbol Shift em Ctrl
static void handle_sdl_key(SDL_Scancode sc, bool pressed) {
    switch (sc) {
      case SDL_SCANCODE_RSHIFT:
      case SDL_SCANCODE_LCTRL:
      case SDL_SCANCODE_RCTRL:
        update_key(7,1,pressed); break;  // Symbol Shift

      case SDL_SCANCODE_SPACE:  update_key(7,0,pressed); break;
      case SDL_SCANCODE_M:      update_key(7,2,pressed); break;
      case SDL_SCANCODE_N:      update_key(7,3,pressed); break;
      case SDL_SCANCODE_B:      update_key(7,4,pressed); break;

      case SDL_SCANCODE_RETURN: update_key(6,0,pressed); break;
      case SDL_SCANCODE_L:      update_key(6,1,pressed); break;
      case SDL_SCANCODE_K:      update_key(6,2,pressed); break;
      case SDL_SCANCODE_J:      update_key(6,3,pressed); break;
      case SDL_SCANCODE_H:      update_key(6,4,pressed); break;

      case SDL_SCANCODE_P: update_key(5,0,pressed); break;
      case SDL_SCANCODE_O: update_key(5,1,pressed); break;
      case SDL_SCANCODE_I: update_key(5,2,pressed); break;
      case SDL_SCANCODE_U: update_key(5,3,pressed); break;
      case SDL_SCANCODE_Y: update_key(5,4,pressed); break;

      case SDL_SCANCODE_0: update_key(4,0,pressed); break;
      case SDL_SCANCODE_9: update_key(4,1,pressed); break;
      case SDL_SCANCODE_8: update_key(4,2,pressed); break;
      case SDL_SCANCODE_7: update_key(4,3,pressed); break;
      case SDL_SCANCODE_6: update_key(4,4,pressed); break;

      case SDL_SCANCODE_1: update_key(3,0,pressed); break;
      case SDL_SCANCODE_2: update_key(3,1,pressed); break;
      case SDL_SCANCODE_3: update_key(3,2,pressed); break;
      case SDL_SCANCODE_4: update_key(3,3,pressed); break;
      case SDL_SCANCODE_5: update_key(3,4,pressed); break;

      case SDL_SCANCODE_Q: update_key(2,0,pressed); break;
      case SDL_SCANCODE_W: update_key(2,1,pressed); break;
      case SDL_SCANCODE_E: update_key(2,2,pressed); break;
      case SDL_SCANCODE_R: update_key(2,3,pressed); break;
      case SDL_SCANCODE_T: update_key(2,4,pressed); break;

      case SDL_SCANCODE_A: update_key(1,0,pressed); break;
      case SDL_SCANCODE_S: update_key(1,1,pressed); break;
      case SDL_SCANCODE_D: update_key(1,2,pressed); break;
      case SDL_SCANCODE_F: update_key(1,3,pressed); break;
      case SDL_SCANCODE_G: update_key(1,4,pressed); break;

      case SDL_SCANCODE_LSHIFT: update_key(0,0,pressed); break;
      case SDL_SCANCODE_Z:      update_key(0,1,pressed); break;
      case SDL_SCANCODE_X:      update_key(0,2,pressed); break;
      case SDL_SCANCODE_C:      update_key(0,3,pressed); break;
      case SDL_SCANCODE_V:      update_key(0,4,pressed); break;

      default: break;
    }
}

// callbacks de memória / I/O para o Z80
static uint8_t read_byte(void* _, uint16_t addr) { return memory[addr]; }
static void write_byte(void* _, uint16_t addr, uint8_t val) {
    if (addr >= ROM_SIZE) memory[addr] = val;
}

// IN A,(0xFE): matrix de teclado
static uint8_t port_in(z80* cpu, uint8_t port_lo) {
    if (port_lo & 1) return 0xFF;
    uint8_t sel = ~cpu->b;
    uint8_t res = 0xFF;
    for (int row = 0; row < 8; row++)
        if (sel & (1 << row)) res &= key_matrix[row];
    return res | 0xE0;
}

// OUT (0xFE),A: border
static void port_out(z80* cpu, uint8_t port_lo, uint8_t val) {
    if ((port_lo & 1) == 0) border_color = (val >> 1) & 0x07;
}

// carrega ROM
static void load_rom(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror("ROM"); exit(1); }
    if (fread(memory,1,ROM_SIZE,f) != ROM_SIZE) { fprintf(stderr,"ROM inválida\n"); exit(1); }
    fclose(f);
    memset(memory + ROM_SIZE, 0, 65536 - ROM_SIZE);
}

int main(int argc, char* argv[]) {
    load_rom("48.rom");
    init_keyboard();

    // CPU
    z80 cpu;
    z80_init(&cpu);
    cpu.read_byte  = read_byte;
    cpu.write_byte = write_byte;
    cpu.port_in    = port_in;
    cpu.port_out   = port_out;
    cpu.userdata   = NULL;
    cpu.pc         = 0x0000;

    // SDL2
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window*   win = SDL_CreateWindow("ZX Spectrum 48K",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W*2, SCREEN_H*2, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED);
    SDL_Texture*  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

    uint32_t framebuf[SCREEN_W*SCREEN_H];
    static const uint32_t palette[16] = {
        0xFF000000,0xFF0000D7,0xFFD70000,0xFFD700D7,
        0xFF00D700,0xFF00D7D7,0xFFD7D700,0xFFD7D7D7,
        0xFF000000,0xFF0000FF,0xFFFF0000,0xFFFF00FF,
        0xFF00FF00,0xFF00FFFF,0xFFFFFF00,0xFFFFFFFF
    };

    bool running = true;
    SDL_Event ev;
    while (running) {
        // flash (~16 frames)
        if (++flash_counter >= 16) { flash_counter = 0; flash_state = !flash_state; }

        Uint32 frameStart = SDL_GetTicks();

        // input
        while (SDL_PollEvent(&ev)) {
            if      (ev.type == SDL_QUIT)    running = false;
            else if (ev.type == SDL_KEYDOWN) handle_sdl_key(ev.key.keysym.scancode, true);
            else if (ev.type == SDL_KEYUP)   handle_sdl_key(ev.key.keysym.scancode, false);
        }

        // emulação CPU
        unsigned long start = cpu.cyc;
        while (cpu.cyc - start < CYCLES_PER_FRAME) z80_step(&cpu);
        z80_gen_int(&cpu, 0);

        // desenhar border
        uint32_t bc = palette[border_color];
        for (int i = 0; i < SCREEN_W*SCREEN_H; i++) framebuf[i] = bc;

        // desenhar bitmap + atributos
        for (int y=0; y<SCREEN_H; y++) {
            int y0 = (y&0xC0)<<5 | (y&0x07)<<8 | (y&0x38)<<2;
            for (int x=0; x<SCREEN_W; x++) {
                uint8_t bit = memory[0x4000+y0+(x>>3)] & (0x80>>(x&7));
                uint8_t A   = memory[0x5800+(y/8)*32+(x/8)];
                bool    br  = (A & 0x40) != 0;
                bool    fl  = (A & 0x80) != 0;
                uint8_t ink = A & 0x07;
                uint8_t pap = (A>>3) & 0x07;
                if (fl && flash_state) { uint8_t tmp = ink; ink = pap; pap = tmp; }
                uint8_t col = (bit ? ink : pap) + (br ? 8 : 0);
                framebuf[y*SCREEN_W + x] = palette[col];
            }
        }

        SDL_UpdateTexture(tex,NULL,framebuf,SCREEN_W*sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren,tex,NULL,NULL);
        SDL_RenderPresent(ren);

        // throttle 50Hz
        Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < 20) SDL_Delay(20-frameTime);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
