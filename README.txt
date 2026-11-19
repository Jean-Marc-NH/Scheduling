Lab: local instruction scheduling (CS342)

Compilar:
  g++ -std=c++17 -O2 -o sched sched.cpp

Usar:
  ./sched <unidades> < bloque.iloc
  ./sched 1 -r < t1.iloc      renombrado
  ./sched 1 -b < t1.iloc      backward list scheduling
  ./sched 1 -rbf < t1.iloc    5 forward + 5 backward al azar, el mas corto
  ./sched 1 -g < t2.iloc      muestra tambien las aristas del grafo

Resultados con 1 unidad:
            normal   -r
  t1          17     13
  t2          12      9
  t3          16     16

Pruebas propias (una por tipo de dependencia):
  p1_verdadera.iloc  solo dependencias verdaderas
  p2_anti.iloc       add lee r1 y despues loadI escribe r1
  p3_salida.iloc     dos escrituras seguidas a r1
  p4_memoria.iloc    store->load misma direccion, load->store misma direccion,
                     misma base con otro desplazamiento (no hay arista) y
                     bases distintas (se asume conflicto)

Por que t3 no cambia con -r?
  En t3 cada registro se escribe una sola vez (r1, r2, r3 y r4), asi que no
  hay dependencias anti ni de salida que quitar. Renombrar solo cambia nombres,
  el grafo queda igual. Lo que alarga t3 son las dependencias de memoria: el
  storeAI r2 => r5,@d usa otra base, entonces se asume que choca con todos los
  loads y stores, y eso obliga a ejecutar casi todo en orden. El renombrado
  de registros no toca la memoria, por eso sigue en 16.
