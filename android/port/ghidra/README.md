# Export de Ghidra

`SLUS_203.93.gzf` es el programa principal del proyecto Ghidra
`Onimusha2` exportado con **File -> Export Program -> Ghidra Zip File** desde
Ghidra 12.2. Conserva el listado, los tipos, las funciones y las referencias
del análisis, y sirve para reabrir el programa sin depender del `.rep`.

- Formato: Ghidra Zip File (`.gzf`)
- Programa: `/SLUS_203.93`
- Exportado en modo headless con `android/port/ghidra/ExportGzf.java`

Para generarlo con tu propio disco:

```powershell
# Proyecto Ghidra con SLUS_203.93 ya importado y analizado
tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat `
  tools\ghidra_projects Onimusha2 -process SLUS_203.93 -noanalysis `
  -scriptPath android\port\ghidra -postScript ExportGzf.java `
  android\port\ghidra\SLUS_203.93.gzf
```

Para importarlo en Ghidra:

1. `File -> Import File...`
2. Elegir `SLUS_203.93.gzf`
3. Abrir el programa importado.

El archivo contiene código y análisis derivados del juego. No se distribuye
con el repositorio público: genéralo localmente y no lo subas.
