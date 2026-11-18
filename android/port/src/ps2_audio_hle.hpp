#pragma once

// HLE de audio (libsd / sceSdRemote).
//
// El ejecutable de PS2 usa libsd remoto por SIF RPC. Sin un IOP real,
// sceSdRemoteInit() se queda en un bucle de sceSifBindRpc(0x80000701) y
// sceSdRemote()/sceSdTransToIOP() esperan la respuesta del servidor LIBSD.
//
// El bloque libsd nativo en SLUS_203.93 es:
//
//   0x0025C650 .. 0x0025C94F
//
// Este modulo implementa el lado nativo:
//   - sceSdRemoteInit / sceSdInit abren Ps2AudioSystem (SDL si esta
//     disponible).
//   - sceSdTransToIOP, sceSdVoiceTrans y sceSdBlockTrans copian bancos de
//     efectos a la RAM SPU2 emulada.
//   - sceSdSetParam/SetSwitch/SetAddr/SetCoreAttr controlan las voces.
//   - sceSdRemote sigue registrando sus comandos; el dispatcher RPC exacto
//     (0x80xx) queda pendiente de mapear.
//
// 0x0025C610..0x0025C64F son helpers del juego pegados a libsd (thunk a
// SignalSema, arranque de thread y lectura de un flag GP) y no se stubbed.
// 0x0025C950 ya es codigo de juego (no ampliar el rango).

#include <cstddef>
#include <cstdint>

namespace ps2::audio {

constexpr std::uint32_t kLibSdBegin = 0x0025C650u;
constexpr std::uint32_t kLibSdEnd = 0x0025C950u;

bool IsStubbedAddress(std::uint32_t address);

// Registra una vez la direccion y devuelve 0.
int Stub(std::uint32_t address);

void ResetLog();

// Memoria EE para resolver los punteros que llegan a sceSdTransToIOP.
// main.cpp la engancha a game::Ram() despues de inicializar el juego.
void SetEeMemory(const std::uint8_t *ram, std::uint32_t size);

// Ejecuta una llamada libsd con sus argumentos. Devuelve true si la
// direccion pertenece al bloque libsd. Los comandos conocidos (init,
// TransToIOP) se aplican al Ps2AudioSystem; el resto sigue siendo stub.
bool Invoke(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
            std::uint32_t a2, std::uint32_t a3, std::uint32_t *result);

// Alimenta la cola de streaming con bloques ADPCM leidos de disc.iso.
bool StreamAdpcm(const void *data, std::size_t bytes, bool loop = false);

}  // namespace ps2::audio

extern "C" {

// libsdr (SIF RPC remoto)
int sceSdRemoteInit(void);
int sceSdRemote(int arg0, ...);
int sceSdTransToIOP(void *eeaddr, void *iopaddr, unsigned int size,
                    unsigned int blocking);
void *sceSdCallBack(void *endfunc);
int sceSdRemoteCallbackInit(int pri);
int sceSdRemoteCallbackQuit(void);

// libsd (API local; stubs para que el enlace no falle si se llama por nombre)
int sceSdInit(int flag);
int sceSdSetParam(unsigned short entry, unsigned short value);
int sceSdGetParam(unsigned short entry);
int sceSdSetSwitch(unsigned short entry, unsigned int value);
int sceSdGetSwitch(unsigned short entry);
int sceSdSetAddr(unsigned short entry, unsigned int value);
int sceSdGetAddr(unsigned short entry);
int sceSdSetCoreAttr(unsigned short entry, unsigned short value);
int sceSdGetCoreAttr(unsigned short entry);
int sceSdVoiceTrans(short core, unsigned char mode, void *mem, unsigned int size);
int sceSdBlockTrans(short core, unsigned char mode, void *mem, unsigned int size,
                    unsigned short arg);
int sceSdVoiceTransStatus(short core, short flag);
int sceSdBlockTransStatus(short core, short flag);
int sceSdStopTrans(short core);

}  // extern "C"
