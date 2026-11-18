#pragma once

// Autotest opcional de almacenamiento. Se compila solo con
// -DPORT_STORAGE_SELFTEST=ON y se llama desde SDL_main.
void port_storage_self_test();
