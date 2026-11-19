#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <random>
#include <chrono>
#include <cctype>
using namespace std;

struct Op {
    string opcode;
    vector<string> izq;      // lo que va antes de =>
    vector<string> der;      // lo que va despues de =>
    vector<string> lee;
    vector<string> escribe;
    int lat = 1;
    bool esLoad = false, esStore = false;
    string base, desp;       // solo para loads y stores
};

struct Arista {
    int otro;      // en suc es el destino, en pred es el origen
    int retardo;
    string tipo;
};

vector<Op> ops;
vector<vector<Arista>> suc, pred;

// ---------------- parser ----------------

string recortar(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

vector<string> separarComas(const string& s) {
    vector<string> res;
    string pedazo;
    stringstream ss(s);
    while (getline(ss, pedazo, ',')) {
        pedazo = recortar(pedazo);
        if (!pedazo.empty()) res.push_back(pedazo);
    }
    return res;
}

bool esRegistro(const string& s) {
    if (s.size() < 2 || s[0] != 'r') return false;
    for (size_t i = 1; i < s.size(); i++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

int latencia(const string& opc) {
    if (opc == "loadAI" || opc == "storeAI" || opc == "load" || opc == "store") return 3;
    if (opc == "mult") return 2;
    return 1;  // loadI, add, sub, lshift, rshift
}

// lee una linea y separa opcode y operandos, false si la linea no tiene nada
bool leerLinea(string linea, Op& op) {
    // powershell mete un BOM al inicio cuando se usa con pipe
    if (linea.rfind("\xEF\xBB\xBF", 0) == 0) linea = linea.substr(3);
    size_t com = linea.find("//");
    if (com != string::npos) linea = linea.substr(0, com);
    linea = recortar(linea);
    if (linea.empty()) return false;

    string izq = linea, der;
    size_t flecha = linea.find("=>");
    if (flecha != string::npos) {
        izq = linea.substr(0, flecha);
        der = linea.substr(flecha + 2);
    }
    stringstream ss(izq);
    ss >> op.opcode;
    string resto;
    getline(ss, resto);
    op.izq = separarComas(resto);
    op.der = separarComas(der);
    return op.opcode != "nop";
}

// a partir de los operandos saca leidos, escritos, base y desplazamiento
void analizar(Op& op) {
    op.lee.clear();
    op.escribe.clear();
    op.lat = latencia(op.opcode);
    op.esStore = op.opcode.rfind("store", 0) == 0;
    op.esLoad = op.opcode.rfind("load", 0) == 0 && op.opcode != "loadI";

    for (auto& t : op.izq)
        if (esRegistro(t)) op.lee.push_back(t);
    for (auto& t : op.der) {
        if (!esRegistro(t)) continue;
        // en un store lo de la derecha es la direccion, se lee
        if (op.esStore) op.lee.push_back(t);
        else op.escribe.push_back(t);
    }

    if (op.esLoad) {
        op.base = op.izq.size() > 0 ? op.izq[0] : "";
        op.desp = op.izq.size() > 1 ? op.izq[1] : "0";
    } else if (op.esStore) {
        op.base = op.der.size() > 0 ? op.der[0] : "";
        op.desp = op.der.size() > 1 ? op.der[1] : "0";
    }
}

string textoOp(const Op& op) {
    string s = op.opcode;
    for (size_t i = 0; i < op.izq.size(); i++)
        s += (i == 0 ? " " : ",") + op.izq[i];
    if (!op.der.empty()) {
        s += " =>";
        for (size_t i = 0; i < op.der.size(); i++)
            s += (i == 0 ? " " : ",") + op.der[i];
    }
    return s;
}

// ---------------- renombrado ----------------

// cada escritura recibe un registro nuevo (menos r0), las lecturas usan el nombre vigente
void renombrar() {
    int mayor = 0;
    for (auto& op : ops)
        for (auto* lado : {&op.izq, &op.der})
            for (auto& t : *lado)
                if (esRegistro(t)) mayor = max(mayor, stoi(t.substr(1)));

    int siguiente = mayor + 1;
    map<string, string> vigente;
    for (auto& op : ops) {
        bool store = op.opcode.rfind("store", 0) == 0;
        // primero las lecturas, con el nombre que tenian antes de esta op
        for (auto& t : op.izq)
            if (esRegistro(t) && vigente.count(t)) t = vigente[t];
        if (store) {
            for (auto& t : op.der)
                if (esRegistro(t) && vigente.count(t)) t = vigente[t];
            continue;
        }
        for (auto& t : op.der) {
            if (!esRegistro(t) || t == "r0") continue;
            string nuevo = "r" + to_string(siguiente++);
            vigente[t] = nuevo;
            t = nuevo;
        }
    }
}

// ---------------- grafo ----------------

void agregarArista(int i, int j, int d, const string& tipo) {
    // si ya hay arista entre i y j se queda la de mayor retardo
    for (auto& a : suc[i]) {
        if (a.otro != j) continue;
        if (d > a.retardo) {
            a.retardo = d;
            a.tipo = tipo;
            for (auto& b : pred[j])
                if (b.otro == i) { b.retardo = d; b.tipo = tipo; }
        }
        return;
    }
    suc[i].push_back({j, d, tipo});
    pred[j].push_back({i, d, tipo});
}

bool chocanMemoria(const Op& a, const Op& b) {
    if (a.esLoad && b.esLoad) return false;
    if (a.base != b.base) return true;   // no sabemos, asumimos que si
    return a.desp == b.desp;
}

void armarGrafo() {
    int n = ops.size();
    suc.assign(n, {});
    pred.assign(n, {});

    map<string, int> ultimoEscritor;
    map<string, vector<int>> lectores;   // los que leyeron desde la ultima escritura
    vector<int> memoria;                 // loads y stores vistos hasta ahora

    for (int j = 0; j < n; j++) {
        Op& b = ops[j];

        for (auto& r : b.lee)
            if (ultimoEscritor.count(r)) {
                int i = ultimoEscritor[r];
                agregarArista(i, j, ops[i].lat, "verdadera");
            }

        for (auto& r : b.escribe) {
            for (int i : lectores[r])
                if (i != j) agregarArista(i, j, 1, "anti");
            if (ultimoEscritor.count(r)) {
                int i = ultimoEscritor[r];
                agregarArista(i, j, ops[i].lat, "salida");
            }
        }

        if (b.esLoad || b.esStore) {
            for (int i : memoria) {
                Op& a = ops[i];
                if (!chocanMemoria(a, b)) continue;
                // store antes: hay que esperar a que termine, load antes: basta 1 ciclo
                int d = a.esStore ? a.lat : 1;
                agregarArista(i, j, d, "memoria");
            }
            memoria.push_back(j);
        }

        // actualizar estado recien despues de sacar las aristas de j
        for (auto& r : b.lee) lectores[r].push_back(j);
        for (auto& r : b.escribe) {
            ultimoEscritor[r] = j;
            lectores[r].clear();
        }
    }
}

void imprimirGrafo() {
    cout << "grafo:\n";
    for (int i = 0; i < (int)ops.size(); i++)
        for (auto& a : suc[i])
            cout << "  " << i + 1 << " -> " << a.otro + 1
                 << "  " << a.tipo << ", " << a.retardo << "\n";
    cout << "\n";
}

// ---------------- prioridades ----------------

// prio(n) = lat(n) + max prio de sus sucesores
vector<int> prioridadAdelante() {
    int n = ops.size();
    vector<int> p(n, 0);
    for (int i = n - 1; i >= 0; i--) {
        int m = 0;
        for (auto& a : suc[i]) m = max(m, p[a.otro]);
        p[i] = ops[i].lat + m;
    }
    return p;
}

// para el backward: camino mas largo desde alguna raiz hasta n
vector<int> prioridadAtras() {
    int n = ops.size();
    vector<int> p(n, 0);
    for (int j = 0; j < n; j++)
        for (auto& a : pred[j]) p[j] = max(p[j], p[a.otro] + a.retardo);
    return p;
}

// ---------------- scheduling ----------------

// desempate[v] menor gana cuando las prioridades son iguales
vector<int> listaAdelante(int unidades, const vector<int>& prio, const vector<double>& desempate) {
    int n = ops.size();
    vector<int> S(n, 0);
    int hechos = 0;
    for (int ciclo = 1; hechos < n; ciclo++) {
        vector<int> listos;
        for (int v = 0; v < n; v++) {
            if (S[v]) continue;
            bool ok = true;
            for (auto& a : pred[v])
                if (!S[a.otro] || S[a.otro] + a.retardo > ciclo) { ok = false; break; }
            if (ok) listos.push_back(v);
        }
        sort(listos.begin(), listos.end(), [&](int x, int y) {
            if (prio[x] != prio[y]) return prio[x] > prio[y];
            return desempate[x] < desempate[y];
        });
        for (int k = 0; k < (int)listos.size() && k < unidades; k++) {
            S[listos[k]] = ciclo;
            hechos++;
        }
    }
    return S;
}

// mismo algoritmo pero sobre el grafo al reves, contando ciclos desde el final.
// para i -> j con retardo d hace falta T(i) >= T(j) + d, y T(v) >= lat(v)
// para que el resultado de v alcance a estar listo al terminar el bloque
vector<int> listaAtras(int unidades, const vector<int>& prio, const vector<double>& desempate) {
    int n = ops.size();
    vector<int> T(n, 0);
    int hechos = 0, maxT = 0;
    for (int ciclo = 1; hechos < n; ciclo++) {
        vector<int> listos;
        for (int v = 0; v < n; v++) {
            if (T[v] || ops[v].lat > ciclo) continue;
            bool ok = true;
            for (auto& a : suc[v])
                if (!T[a.otro] || T[a.otro] + a.retardo > ciclo) { ok = false; break; }
            if (ok) listos.push_back(v);
        }
        sort(listos.begin(), listos.end(), [&](int x, int y) {
            if (prio[x] != prio[y]) return prio[x] > prio[y];
            return desempate[x] < desempate[y];
        });
        for (int k = 0; k < (int)listos.size() && k < unidades; k++) {
            T[listos[k]] = ciclo;
            maxT = ciclo;
            hechos++;
        }
    }
    // pasar a ciclos normales
    vector<int> S(n);
    for (int v = 0; v < n; v++) S[v] = maxT + 1 - T[v];
    return S;
}

int largo(const vector<int>& S) {
    int m = 1;
    for (int v = 0; v < (int)ops.size(); v++) m = max(m, S[v] + ops[v].lat);
    return m - 1;
}

// revisa que se respeten aristas y unidades, por si acaso
bool valido(const vector<int>& S, int unidades) {
    map<int, int> usados;
    for (int v = 0; v < (int)ops.size(); v++) {
        if (S[v] < 1 || ++usados[S[v]] > unidades) return false;
        for (auto& a : suc[v])
            if (S[v] + a.retardo > S[a.otro]) return false;
    }
    return true;
}

void imprimir(const vector<int>& S) {
    int L = largo(S);
    for (int c = 1; c <= L; c++) {
        string linea;
        for (int v = 0; v < (int)ops.size(); v++)
            if (S[v] == c) linea += (linea.empty() ? "" : "; ") + textoOp(ops[v]);
        cout << c << ": " << (linea.empty() ? "nop" : linea) << "\n";
    }
    cout << "length: " << L << " cycles\n";
}

int main(int argc, char** argv) {
    int unidades = 0;
    bool renom = false, atras = false, rbf = false, verGrafo = false;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "-r") renom = true;
        else if (a == "-b") atras = true;
        else if (a == "-rbf") rbf = true;
        else if (a == "-g") verGrafo = true;
        else unidades = atoi(a.c_str());
    }
    if (unidades < 1) {
        cerr << "uso: ./sched <unidades> [-r] [-b] [-rbf] [-g] < bloque.iloc\n";
        return 1;
    }

    string linea;
    while (getline(cin, linea)) {
        Op op;
        if (leerLinea(linea, op)) ops.push_back(op);
    }
    if (ops.empty()) {
        cout << "length: 0 cycles\n";
        return 0;
    }

    if (renom) renombrar();
    for (auto& op : ops) analizar(op);
    armarGrafo();
    if (verGrafo) imprimirGrafo();

    int n = ops.size();
    vector<int> pAdelante = prioridadAdelante();
    vector<int> pAtras = prioridadAtras();

    // empates normales: forward gana la que aparece primero, backward la que aparece ultimo
    vector<double> orden(n), ordenInv(n);
    for (int v = 0; v < n; v++) {
        orden[v] = v;
        ordenInv[v] = -v;
    }

    vector<int> S;
    if (rbf) {
        mt19937 rng(chrono::steady_clock::now().time_since_epoch().count());
        uniform_real_distribution<double> azar(0.0, 1.0);
        int mejor = -1;
        string cual;
        for (int intento = 0; intento < 10; intento++) {
            vector<double> d(n);
            for (auto& x : d) x = azar(rng);
            bool esAtras = intento >= 5;
            vector<int> P = esAtras ? listaAtras(unidades, pAtras, d)
                                    : listaAdelante(unidades, pAdelante, d);
            int L = largo(P);
            cerr << (esAtras ? "backward " : "forward  ") << intento % 5 + 1 << ": " << L << "\n";
            if (mejor < 0 || L < mejor) {
                mejor = L;
                S = P;
                cual = esAtras ? "backward" : "forward";
            }
        }
        cerr << "se queda: " << cual << ", " << mejor << " ciclos\n";
    } else if (atras) {
        S = listaAtras(unidades, pAtras, ordenInv);
    } else {
        S = listaAdelante(unidades, pAdelante, orden);
    }

    if (!valido(S, unidades)) cerr << "ojo: el schedule no respeta alguna dependencia\n";
    imprimir(S);
    return 0;
}

//   g++ -std=c++17 -O2 -o s sched.cpp
//
//   s.exe 1 < t1.iloc          forward con 1 unidad
//   s.exe 2 < t1.iloc          forward con 2 unidades
//   s.exe 1 -r < t1.iloc       con renombrado
//   s.exe 1 -b < t1.iloc       backward
//   s.exe 1 -rbf < t1.iloc     5 forward + 5 backward al azar, el mas corto
//   s.exe 1 -g < t2.iloc       ademas imprime las aristas del grafo
