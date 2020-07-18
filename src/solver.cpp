#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>
#include <fstream>

#include "include/solver.hpp"
#include "include/vector_arithmetics.hpp"

#include "muParser.h"       // Pour transformer des expressions (chaines de caracteres) en fonctions

using namespace mu;
using namespace std;

/* Type utilisé pour le stockage des signaux */
typedef std::vector<double> vector_1row;                // de taille n_cells ou time_steps
typedef std::vector<double> vector_2d;                  // de taille 2 (x et y)
typedef std::vector<vector_2d> vector_2rows;            // de taille n_cells * 2


/**
 * Allocate space for 2D matrix
 */ 
double ** allocate(int nrows, int ncols){
    double ** matrix = new double*[nrows];
    for (int i = 0; i < nrows; i++)
        matrix[i] = new double[ncols];
    return matrix;
}


// void allocate(double ***matrix, int nrows, int ncols){
//     *matrix = new double*[nrows];
//     for (int i = 0; i < nrows; i++)
//         (*matrix)[i] = new double[ncols]; 
// }

/**
 * Free space from 2D matrix
 */ 
void free(double **matrix, int nrows){
    for (int i = 0; i < nrows; i++)
        delete[] matrix[i];
    delete[] matrix;
}


Solver::Solver(const Mesh *new_mesh, const Config &cfg){
    mesh = new_mesh;

    c = atof(cfg.values.at("c").c_str());
    a = atof(cfg.values.at("a").c_str());

    C_v = atof(cfg.values.at("C_v").c_str());

    CFL = atof(cfg.values.at("CFL").c_str());
    precision = atof(cfg.values.at("precision").c_str());
    t_0 = atof(cfg.values.at("t_0").c_str());
    t_f = atof(cfg.values.at("t_f").c_str());

    rho_expr = cfg.values.at("rho");
    sigma_a_expr = cfg.values.at("sigma_a");
    sigma_c_expr = cfg.values.at("sigma_c");

    rho_vec = vector_1row(mesh->n_cells);

    E = vector_1row(mesh->n_cells);
    F = vector_2rows(mesh->n_cells, vector_2d(2));
    T = vector_1row(mesh->n_cells);

    E_0_expr = cfg.values.at("E_0");
    F_0_x_expr = cfg.values.at("F_0_x");
    F_0_y_expr = cfg.values.at("F_0_y");
    T_0_expr = cfg.values.at("T_0");

    E_u_expr = cfg.values.at("E_u");
    F_u_x_expr = cfg.values.at("F_u_x");
    F_u_y_expr = cfg.values.at("F_u_y");
    T_u_expr = cfg.values.at("T_u");

    E_d_expr = cfg.values.at("E_d");
    F_d_x_expr = cfg.values.at("F_d_x");
    F_d_y_expr = cfg.values.at("F_d_y");
    T_d_expr = cfg.values.at("T_d");

    E_l_expr = cfg.values.at("E_l");
    F_l_x_expr = cfg.values.at("F_l_x");
    F_l_y_expr = cfg.values.at("F_l_y");
    T_l_expr = cfg.values.at("T_l");

    E_r_expr = cfg.values.at("E_r");
    F_r_x_expr = cfg.values.at("F_r_x");
    F_r_y_expr = cfg.values.at("F_r_y");
    T_r_expr = cfg.values.at("T_r");

    E_exact_expr = cfg.values.at("E_exact");
    F_exact_x_expr = cfg.values.at("F_exact_x");
    F_exact_y_expr = cfg.values.at("F_exact_y");
    T_exact_expr = cfg.values.at("T_exact");

    /* Verifications preliminaires */
    if (c <= 0)
        throw string("ERREUR: Vérifiez que c > 0");
    if (a <= 0)
        throw string("ERREUR: Vérifiez que a > 0");
    if (C_v <= 0)
        throw string("ERREUR: Vérifiez que C_v > 0");
    if (CFL <= 0)
        throw string("ERREUR: Vérifiez que CFL > 0");
    if (precision <= 0)
        throw string("ERREUR: Vérifiez que la precision est > 0");
    if (t_0 < 0)
        throw string("ERREUR: Vérifiez que t_0 >= 0");
    if (t_f <= 0)
        throw string("ERREUR: Vérifiez que t_f > 0");

    dt = CFL * mesh->dx/c;
    // dt = CFL * mesh->dx * mesh->dy /c;                  // ****!!***!!*********CFL condition comme ceci?
    double tmp = t_f / dt;
    if (tmp == floor(tmp))        // si entier
        step_count = floor(tmp);
    else
        step_count = floor(tmp) + 1;
    time_steps = vector_1row(step_count);

    /* A exporter */
    E_up = allocate(step_count, mesh->N);
    F_up = allocate(step_count, mesh->N);
    T_up = allocate(step_count, mesh->N);

    E_down = allocate(step_count, mesh->N);
    F_down = allocate(step_count, mesh->N);
    T_down = allocate(step_count, mesh->N);

    E_left = allocate(step_count, mesh->M);
    F_left = allocate(step_count, mesh->M);
    T_left = allocate(step_count, mesh->M);

    E_right = allocate(step_count, mesh->M);
    F_right = allocate(step_count, mesh->M);
    T_right = allocate(step_count, mesh->M);
} 


/**
 * Laplacian smoothing (k-means) d'un vecteur, avec k=3
 */ 
vector_1row smooth(vector_1row& input, int **neighb){
    int size = input.size();
    vector_1row output(size);

    for (int k = 0; k < size; k++){
        double sum = input[k];
        int count_nbr = 1;

        for (int l = 0; l < 4; l++){
            int id_nbr = neighb[k][l];
            if ( id_nbr != -1){
                sum += input[id_nbr];
                count_nbr += 1;
            }
        }

        output[k] = sum / count_nbr;
    }

    return output;
}


vector_1row Solver::niche(int nb_niche, double z_min, double z_max, int n_smooth){
    /* Vecteur qui va contenir le signal en crenaux */
    vector_1row signal(mesh->n_cells);

    /* Les attributs du signal */
    n_niche = nb_niche;
    attr = allocate(n_niche, 4);

    srand(time(NULL));
    for (int l = 0; l < n_niche; l++){
        /* Attributs des crenaux pris au hazard */
        // attr[k][0] = rand() % (N-1) + 1;                     // position
        // attr[k][1] = rand() % (N/3) + 5;                  // largeur
        // attr[k][2] = ((double) rand() / (RAND_MAX)) * (y_max-1.) + 1;    // hauteur
        
        /* Les memes attributs a chaque fois */
        attr[l][0] = (0.7*mesh->N-1)*mesh->dx + mesh->dx/2. + mesh->x_min ;  // abcisse
        attr[l][1] =  (0.5*mesh->M-1)*mesh->dy + mesh->dy/2. + mesh->y_min; // ordonnee
        attr[l][2] = 0.1*((mesh->N + mesh->M)/2) * ((mesh->dx + mesh->dy)/2); // diametre
        attr[l][3] = z_max;                                 // hauteur
    }

    /* Placement des crenaux */
    for (int i = 0; i < mesh->N+2; i++){
        for (int j = 0; j < mesh->M+2; j++){
            int k = cell_id(i, j, mesh->N+2, mesh->M+2);
            signal[k] = z_min;
            for (int l = 0; l < n_niche; l++){
                if (sqrt(pow(mesh->x[i] - attr[l][0], 2) + pow(mesh->y[j] - attr[l][1], 2)) <= attr[l][2]/2.){
                    signal[k] = attr[l][3];
                    break;
                }
            }
        }
    }

    /* Lissage du signal */
    for (int i = 0; i < n_smooth; i++)
        signal = smooth(signal, mesh->neighb);

    return signal;
}


double Solver::rho(double x, double y){
    static int first_call = 1;
    static int custom = rho_expr.compare("custom");

    if (custom == 0){
        // static vector_1row signal(mesh->N+2);
        if (first_call == 1){rho_vec = niche(1, 0.01, 1.0, 0.1*(mesh->N + mesh->M)/2.); first_call = 0;}
        int i = int((x - mesh->x_min + mesh->dx/2.) / mesh->dx);
        int j = int((y - mesh->y_min + mesh->dy/2.) / mesh->dy);
        int k = cell_id(i, j, mesh->N+2, mesh->M+2);
        return rho_vec[k];
    }
    else{
        static Parser p;
        p.DefineVar("x", &x);
        p.DefineVar("y", &y);
        if (first_call == 1){p.SetExpr(rho_expr); first_call = 0;}
        return p.Eval();
    }
}


double Solver::sigma_a(double rho, double T){
    static int first_call = 1;

    static Parser p;
    p.DefineVar("rho", &rho); 
    p.DefineVar("T", &T); 
    if (first_call == 1){ p.SetExpr(sigma_a_expr); first_call = 0; }

    return p.Eval();
}


double Solver::sigma_c(double rho, double T){
    static int first_call = 1;

    static Parser p;
    p.DefineVar("rho", &rho);
    p.DefineVar("T", &T); 
    if (first_call == 1){ p.SetExpr(sigma_c_expr); first_call = 0; }

    return p.Eval();
}


double Solver::E_0(double x, double y){
    static int first_call = 1;

    static Parser p;
    p.DefineVar("x", &x);
    p.DefineVar("y", &y);
    p.DefineVar("t_0", &t_0);
    if (first_call == 1){ p.SetExpr(E_0_expr); first_call = 0; }

    return p.Eval();
}


vector_2d Solver::F_0(double x, double y){
    static int first_call = 1;

    static Parser p1, p2;
    p1.DefineVar("x", &x); p2.DefineVar("x", &x);
    p1.DefineVar("y", &y); p2.DefineVar("y", &y);
    p1.DefineVar("t_0", &t_0); p2.DefineVar("t_0", &t_0);
    if (first_call == 1){ p1.SetExpr(F_0_x_expr); p2.SetExpr(F_0_y_expr); first_call = 0; }

    return vector_2d {p1.Eval(), p2.Eval()};
}


double Solver::T_0(double x, double y){
    static int first_call = 1;

    static Parser p;
    p.DefineVar("x", &x); 
    p.DefineVar("y", &y);
    p.DefineVar("t_0", &t_0);
    if (first_call == 1){ p.SetExpr(T_0_expr); first_call = 0; }

    return p.Eval();
}


double Solver::E_u(double t, double x){
    static int first_call = 1;
    static int neumann = E_u_expr.compare("neumann");

    if (neumann == 0){
        int i = int((x - mesh->x_min + mesh->dx/2.) / mesh->dx);
        int k = cell_id(i, mesh->M, mesh->N+2, mesh->M+2);
        return E[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("x", &x);
        if (first_call == 1){ p.SetExpr(E_u_expr); first_call = 0; }
        return p.Eval();
    }
}


vector_2d Solver::F_u(double t, double x){
    static int first_call = 1;
    static int neumann = F_u_x_expr.compare("neumann");

    if (neumann == 0){
        int i = int((x - mesh->x_min + mesh->dx/2.) / mesh->dx);
        int k = cell_id(i, mesh->M, mesh->N+2, mesh->M+2);
        return F[k];
    }
    else{ 
        static Parser p1, p2;
        p1.DefineVar("t", &t); p2.DefineVar("t", &t);
        p1.DefineVar("x", &x); p2.DefineVar("x", &x);
        if (first_call == 1){ p1.SetExpr(F_u_x_expr); p2.SetExpr(F_u_y_expr); first_call = 0; }
        return vector_2d {p1.Eval(), p2.Eval()};
    }
}


double Solver::T_u(double t, double x){
    static int first_call = 1;
    static int neumann = T_u_expr.compare("neumann");

    if (neumann == 0){
        int i = int((x - mesh->x_min + mesh->dx/2.) / mesh->dx);
        int k = cell_id(i, mesh->M, mesh->N+2, mesh->M+2);
        return T[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("x", &x);
        if (first_call == 1){ p.SetExpr(T_u_expr); first_call = 0; }
        return p.Eval();
    }
}


double Solver::E_d(double t, double x){
    static int first_call = 1;
    static int neumann = E_d_expr.compare("neumann");

    if (neumann == 0){
        int i = int((x - mesh->x_min + mesh->dx/2.) / mesh->dx);
        int k = cell_id(i, 1, mesh->N+2, mesh->M+2);
        return E[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("x", &x);
        if (first_call == 1){ p.SetExpr(E_d_expr); first_call = 0; }
        return p.Eval();
    }
}


vector_2d Solver::F_d(double t, double x){
    static int first_call = 1;
    static int neumann = F_d_x_expr.compare("neumann");

    if (neumann == 0){
        int i = int((x - mesh->x_min + mesh->dx/2.) / mesh->dx);
        int k = cell_id(i, 1, mesh->N+2, mesh->M+2);
        return F[k];
    }
    else{ 
        static Parser p1, p2;
        p1.DefineVar("t", &t); p2.DefineVar("t", &t);
        p1.DefineVar("x", &x); p2.DefineVar("x", &x);
        if (first_call == 1){ p1.SetExpr(F_d_x_expr); p2.SetExpr(F_d_y_expr); first_call = 0; }
        return vector_2d {p1.Eval(), p2.Eval()};
    }
}


double Solver::T_d(double t, double x){
    static int first_call = 1;
    static int neumann = T_d_expr.compare("neumann");

    if (neumann == 0){
        int i = int((x - mesh->x_min + mesh->dx/2.) / mesh->dx);
        int k = cell_id(i, 1, mesh->N+2, mesh->M+2);
        return T[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("x", &x);
        if (first_call == 1){ p.SetExpr(T_d_expr); first_call = 0; }
        return p.Eval();
    }
}


double Solver::E_l(double t, double y){
    static int first_call = 1;
    static int neumann = E_l_expr.compare("neumann");

    if (neumann == 0){
        int j = int((y - mesh->y_min + mesh->dy/2.) / mesh->dy);
        int k = cell_id(1, j, mesh->N+2, mesh->M+2);
        return E[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("y", &y);
        if (first_call == 1){ p.SetExpr(E_l_expr); first_call = 0; }
        return p.Eval();
    }
}


vector_2d Solver::F_l(double t, double y){
    static int first_call = 1;
    static int neumann = F_l_x_expr.compare("neumann");

    if (neumann == 0){
        int j = int((y - mesh->y_min + mesh->dy/2.) / mesh->dy);
        int k = cell_id(1, j, mesh->N+2, mesh->M+2);
        return F[k];
    }
    else{ 
        static Parser p1, p2;
        p1.DefineVar("t", &t); p2.DefineVar("t", &t);
        p1.DefineVar("y", &y); p2.DefineVar("y", &y);
        if (first_call == 1){ p1.SetExpr(F_l_x_expr); p2.SetExpr(F_l_y_expr); first_call = 0; }
        return vector_2d {p1.Eval(), p2.Eval()};
    }
}


double Solver::T_l(double t, double y){
    static int first_call = 1;
    static int neumann = T_l_expr.compare("neumann");

    if (neumann == 0){
        int j = int((y - mesh->y_min + mesh->dy/2.) / mesh->dy);
        int k = cell_id(1, j, mesh->N+2, mesh->M+2);
        return T[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("y", &y);
        if (first_call == 1){ p.SetExpr(T_l_expr); first_call = 0; }
        return p.Eval();
    }
}


double Solver::E_r(double t, double y){
    static int first_call = 1;
    static int neumann = E_r_expr.compare("neumann");

    if (neumann == 0){
        int j = int((y - mesh->y_min + mesh->dy/2.) / mesh->dy);
        int k = cell_id(mesh->N, j, mesh->N+2, mesh->M+2);
        return E[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("y", &y);
        if (first_call == 1){ p.SetExpr(E_r_expr); first_call = 0; }
        return p.Eval();
    }
}


vector_2d Solver::F_r(double t, double y){
    static int first_call = 1;
    static int neumann = F_r_x_expr.compare("neumann");

    if (neumann == 0){
        int j = int((y - mesh->y_min + mesh->dy/2.) / mesh->dy);
        int k = cell_id(mesh->N, j, mesh->N+2, mesh->M+2);
        return F[k];
    }
    else{ 
        static Parser p1, p2;
        p1.DefineVar("t", &t); p2.DefineVar("t", &t);
        p1.DefineVar("y", &y); p2.DefineVar("y", &y);
        if (first_call == 1){ p1.SetExpr(F_r_x_expr); p2.SetExpr(F_r_y_expr); first_call = 0; }
        return vector_2d {p1.Eval(), p2.Eval()};
    }
}


double Solver::T_r(double t, double y){
    static int first_call = 1;
    static int neumann = T_r_expr.compare("neumann");

    if (neumann == 0){
        int j = int((y - mesh->y_min + mesh->dy/2.) / mesh->dy);
        int k = cell_id(mesh->N, j, mesh->N+2, mesh->M+2);
        return T[k];
    }
    else{ 
        static Parser p;
        p.DefineVar("t", &t);
        p.DefineVar("y", &y);
        if (first_call == 1){ p.SetExpr(T_r_expr); first_call = 0; }
        return p.Eval();
    }
}


double Solver::E_exact(double t, double x, double y){
    static int first_call = 1;

    static Parser p;
    p.DefineVar("t", &t);
    p.DefineVar("x", &x);
    p.DefineVar("y", &y);
    p.DefineVar("t_0", &t_0);
    if (first_call == 1){ p.SetExpr(E_exact_expr); first_call = 0; }

    return p.Eval();
}


vector_2d Solver::F_exact(double t, double x, double y){
    static int first_call = 1;

    static Parser p1, p2;
    p1.DefineVar("t", &t); p2.DefineVar("t", &t);
    p1.DefineVar("x", &x); p2.DefineVar("x", &x);
    p1.DefineVar("y", &y); p2.DefineVar("y", &y);
    p1.DefineVar("t_0", &t_0); p2.DefineVar("t_0", &t_0);
    if (first_call == 1){ p1.SetExpr(F_exact_x_expr); p2.SetExpr(F_exact_y_expr); first_call = 0; }

    return vector_2d {p1.Eval(), p2.Eval()};
}


double Solver::T_exact(double t, double x, double y){
    static int first_call = 1;

    static Parser p;
    p.DefineVar("t", &t);
    p.DefineVar("x", &x);
    p.DefineVar("y", &y);
    p.DefineVar("t_0", &t_0);
    if (first_call == 1){ p.SetExpr(T_exact_expr); first_call = 0; }

    return p.Eval();
}


/**
 * Calcule sigma_{k, l}, moyenne de sigma_c entre les cellules k et l
 */ 
double compute_sigma(double sigma_k, double sigma_l){
    return 0.5 * (sigma_k + sigma_l);
}


/**
 * Calcule les M_{k, l}
 */ 
double compute_M(double dx, double sigma_kl){
    return 2 / (2 + dx * sigma_kl);
}


/**
 * Calcule les E_{k, l}n_{k, l}, flux de E entre les cellules k et l
 */ 
vector_2d flux_E(double l_kl, double M_kl, double E_k, double E_l, vector_2d F_k, vector_2d F_l, vector_2d n_kl){
    double tmp1 = 0.5 * (E_k + E_l);
    double tmp2 = 0.5 * (dot(F_l, n_kl) - dot(F_k, n_kl));
    return prod(l_kl*M_kl * (tmp1+tmp2), n_kl);

    // double tmp1 = 0.5 * (E_k + E_l);
    // double tmp2 = -0.5 * (dot(F_l, n_kl) - dot(F_k, n_kl));
    // return prod(l_kl*M_kl * (tmp1+tmp2), n_kl);

    // double tmp1 = 0.5 * (E_k + E_l);
    // double tmp2 = 0.5 * (dot(F_l, n_kl) - dot(F_k, n_kl));
    // return prod(l_kl*M_kl * (tmp1 - tmp2), n_kl);
}


/**
 * Calcule les F_{k, l} . n_{k, l}, flux de F entre les cellules k et l
 */ 
double flux_F(double l_kl, double M_kl, double E_k, double E_l, vector_2d F_k, vector_2d F_l, vector_2d n_kl){
    double tmp1 = 0.5 * (dot(F_k, n_kl) + dot(F_l, n_kl));
    double tmp2 = 0.5 * (E_l - E_k);
    return l_kl*M_kl * (tmp1+tmp2);

    // vector_2d tmp1 = prod(0.5, add(F_k, F_l));
    // vector_2d tmp2 = prod(-0.5, add(prod(E_l, n_kl), prod(-E_k, n_kl)));
    // return dot(prod(l_kl*M_kl, add(tmp1, tmp2)), n_kl);

    // double tmp1 = 0.5 * (dot(F_k, n_kl) + dot(F_l, n_kl));
    // double tmp2 = 0.5 * (E_l - E_k);
    // return l_kl*M_kl * (tmp1 - tmp2);
}


void Solver::save_animation(int time_step){
    string file_name = "data/anim/animation." + to_string(time_step) + ".csv";
    ofstream file;
    file.open(file_name, ios_base::trunc);

    if(!file)
        throw string ("ERREUR: Erreur d'ouverture du fichier '" + file_name + "'");

    file << "E,F_x,F_y,T,Tr\n";

    for (int j = 1; j < mesh->M+1; j++){
        for (int i = 1; i < mesh->N+1; i++){
            int k = cell_id(i, j, mesh->N+2, mesh->M+2);
            file << E[k] << "," << F[k][0] << "," << F[k][1] << "," << T[k] << "," << pow(E[k]/a, 0.25) << "\n";
        }
    }

    /* Rajoutons x et y */
    // file << "\"[";
    // for (int i = 1; i < mesh->N+1; i++){
    //     file << mesh->x[i];
    //     if (i != mesh->N) file << ",";
    // }
    // file << "]\",";

    // file << "\"[";
    // for (int j = 1; j < mesh->M+1; j++){
    //     file << mesh->y[j];
    //     if (j != mesh->M) file << ",";
    // }
    // file << "]\",";

    file.close();
}


void Solver::phase_1(){
    /* Des variables necessaires pour cette etape */
    double Theta;       // Theta = a*T^4 
    double E_n, T_n, Theta_n; 
    double E_next, Theta_next;
    
    // for (int k = 0; k < mesh->n_cells; k++){
    //     int i = mesh->coord[k][0];
    //     int j = mesh->coord[k][1];
    //     if(1 <= i && i <= mesh->N && 1 <= j && j <= mesh->M){

    for (int i = 1; i < mesh->N+1; i++){
        for (int j = 1; j < mesh->M+1; j++){
            int k = cell_id(i, j, mesh->N+2, mesh->M+2);
            // Initialisation etape 1 
            E_n = E[k];
            T_n = T[k];
            Theta_n = a * pow(T_n, 4);
            Theta = Theta_n;

            E_next = E[k];
            Theta_next = Theta;

            do{
                E[k] = E_next;
                Theta = Theta_next;
                
                T[k] = pow(Theta/a, 0.25);
                // T[k] = pow(abs(Theta)/a, 0.25);         //************************** Comme ca ?
                double mu_q = 1/ (pow(T_n, 3) + T_n*pow(T[k], 2) + T[k]*pow(T_n, 2) + pow(T[k], 3));
                bool nan = isnan(mu_q);             //************************************************* A RETIRER
                if (isnan(mu_q))
                    // cerr << "ATTENTION! mu = nan" << " en k = " << k << endl;
                    ;

                double rho_tmp = rho(mesh->x[i], mesh->y[j]);
                double sigma_a_tmp = sigma_a(rho_tmp, T[k]);

                double tmp_1 = (1/dt) + c*sigma_a_tmp;
                double alpha = 1/dt/tmp_1;
                double beta = c*sigma_a_tmp/tmp_1;

                double tmp_2 = (rho_tmp*C_v*mu_q/dt) + c*sigma_a_tmp;
                double gamma = rho_tmp*C_v*mu_q/dt/tmp_2;
                double delta = c*sigma_a_tmp/tmp_2;

                E_next = (alpha*E_n + gamma*beta*Theta_n) / (1 - beta*delta);
                Theta_next = (gamma*Theta_n + alpha*delta*E_n) / (1 - beta*delta);

            } while (abs(E_next-E[k]) > precision && abs(Theta_next-Theta) > precision);
        }
    }
};


// void Solver::phase_1_eq(){
//     /* Des variables necessaires pour cette etape */
//     double Theta;       // Theta = a*T^4
//     double E_n, T_n, Theta_n;
//     double E_next, Theta_next;


//     for (int i = 1; i < mesh->N+1; i++){
//         for (int j = 1; j < mesh->M+1; j++){
//             int k = cell_id(i, j, mesh->N+2, mesh->M+2);

//             // Initialisation
//             E_n = E[k];
//             T_n = T[k];
//             Theta_n = a * pow(T_n, 4);
//             Theta = Theta_n;

//             E_next = E[k];
//             Theta_next = Theta;
//                 // cout << "E = "<< E_next << " Theta = " << Theta_next<< endl;

//             do{
//                 E[k] = E_next;
//                 Theta = Theta_next;

//                 T[k] = pow(Theta/a, 0.25);
//                 double mu_q = 1/ (pow(T_n, 3) + T_n*pow(T[k], 2) + T[k]*pow(T_n, 2) + pow(T[k], 3));
//                 if (isnan(mu_q))
//                     // cerr << "ATTENTION! mu = nan" << " en k = " << k << endl;
//                     ;

//                 double rho_tmp = rho(mesh->x[i], mesh->y[j]);
//                 double alpha = c * sigma_a(rho_tmp, T[k]) * dt;
//                 double beta = rho_tmp * C_v * mu_q;

//                 double X_n = E_n - Theta_n;
//                 double Y_n = E_n + beta*Theta_n;

//                 double X = X_n / (1 + alpha*(1 + (1./beta)));
//                 double Y = Y_n;

//                 E_next = (beta*X + Y) / (1 + beta);
//                 Theta_next = (-X + Y) / (1 + beta);

//             } while (abs(E_next-E[k]) > precision && abs(Theta_next-Theta) > precision);
//         }
//     }
// };


void Solver::phase_2(){
    /* Vecteurs necessaires pour cette etape */
    vector_1row E_etoile(mesh->n_cells);
    vector_1row E_suiv(mesh->n_cells);
    vector_2rows F_etoile(mesh->n_cells, vector_2d(2));
    vector_2rows F_suiv(mesh->n_cells, vector_2d(2));

    /* Initialisation de l'etape */
    E_etoile = E;
    F_etoile = F;

    /* Pour conserver les valeurs des mailles fantomes */
    // E_suiv = E;
    // F_suiv = F;

    // for (int k = 0; k < mesh->n_cells; k++){
    //     int i = mesh->coord[k][0];
    //     int j = mesh->coord[k][1];

    //     if(1 <= i && i <= mesh->N && 1 <= j && j <= mesh->M){

    for (int i = 1; i < mesh->N+1; i++){
        for (int j = 1; j < mesh->M+1; j++){
            int k = cell_id(i, j, mesh->N+2, mesh->M+2);

            vector_2d sum_flux_E {0, 0};
            double sum_flux_F = 0;
            double sum_M_sigma = 0;
            vector_2d sum_l_M_n {0, 0};

            double x_k = mesh->x[i];
            double y_k = mesh->y[j];
            double rho_k = rho(x_k, y_k);

            for (int neighbor = 0; neighbor < 4; neighbor++){
                int l = mesh->neighb[k][neighbor];
                int i_prime = mesh->coord[l][0];
                int j_prime = mesh->coord[l][1];
                double x_l = mesh->x[i_prime];
                double y_l = mesh->y[j_prime];

                double rho_l = rho(x_l, y_l);
                double sigma_kl = compute_sigma(sigma_c(rho_k, T[k]), sigma_c(rho_l, T[l]));
                double M_kl = compute_M(mesh->dx, sigma_kl);

                double l_kl;
                vector_2d n_kl;     // verteur normal
                if (neighbor == 0){                        // Voisin du haut
                    n_kl = {0, 1};
                    l_kl = mesh->dy;
                }
                else if (neighbor == 1){                        // Voisin du bas
                    n_kl = {0, -1};
                    l_kl = mesh->dy;
                }
                else if (neighbor == 2){                        // Voisin de gauche
                    n_kl = {-1, 0};
                    l_kl = mesh->dx;
                }
                else if (neighbor == 3){                        // Voisin de droite
                    n_kl = {1, 0};
                    l_kl = mesh->dx;
                }

                vector_2d flux_E_kl = flux_E(l_kl, M_kl, E[k], E[l], F[k], F[l], n_kl);
                double flux_F_kl = flux_F(l_kl, M_kl, E[k], E[l], F[k], F[l], n_kl);

                sum_flux_E = add(sum_flux_E, flux_E_kl);
                sum_flux_F += flux_F_kl;
                sum_M_sigma += (M_kl*sigma_kl);
                sum_l_M_n = add(sum_l_M_n, prod(l_kl*M_kl, n_kl));
            }

            double mes_omega = mesh->dx * mesh->dy;
            double tmp = (1./dt) + c*sum_M_sigma;
            // double tmp = (1./dt) + c*sum_M_sigma/4;         //*************ALTERNATIVE?
            double alpha = -c*dt / mes_omega;
            double beta = 1./dt / tmp;
            vector_2d gamma = prod(c/mes_omega / tmp, sum_l_M_n);
            double delta = -c/mes_omega / tmp;

            E_suiv[k] = E_etoile[k] + alpha*sum_flux_F;

            // vector_2d tmp1 = prod(beta, F_etoile[k]);
            // vector_2d tmp2 = prod(E[k], gamma);
            // vector_2d tmp4 = add(tmp1, tmp2);
            // F_suiv[k] = add(tmp4, prod(delta, sum_flux_E));
            F_suiv[k] = add(add(prod(beta, F_etoile[k]), prod(E[k], gamma)), prod(delta, sum_flux_E));
        }
    }

    E = E_suiv;
    F = F_suiv;
    
    // int k = 200;
    // cout << "F = " << F[k][0] << ", " << F[k][1] << endl;
    // cout << "F_suiv = " << F_suiv[k][0] << ", " << F_suiv[k][1] << endl;
};


void Solver::solve(){
    /* Initialisation de la doucle de resolution */
    for (int k = 0; k < mesh->n_cells; k++){
        int i = mesh->coord[k][0];
        int j = mesh->coord[k][1];
        double x_k = mesh->x[i];
        double y_k = mesh->y[j];

        E[k] = E_0(x_k, y_k);
        F[k] = F_0(x_k, y_k);
        T[k] = T_0(x_k, y_k);
    }

    /* Temps courant (translaté de t_0) et indice de l'iteration */
    double t = 0;
    int n = 0;

    /**
     * Boucle de resolution
     */
    while (t <= t_f){
    // while (t <= t_f && n < 21){
        /* Enregistrement des signaux pour ce temps */
        save_animation(n);
        cout << " --- iteration " << n << endl;

        /* Signaux exportés */
        for (int i = 1; i < mesh->N+1; i++){
            int k = cell_id(i, mesh->M, mesh->N+2, mesh->M+2);
            E_up[n][i-1] = E[k];
            F_up[n][i-1] = l2_norm(F[k]);
            T_up[n][i-1] = T[k];

            k = cell_id(i, 1, mesh->N+2, mesh->M+2);
            E_down[n][i-1] = E[k];
            F_down[n][i-1] = l2_norm(F[k]);
            T_down[n][i-1] = T[k];
        }
        for (int j = 1; j < mesh->M+1; j++){
            int k = cell_id(1, j, mesh->N+2, mesh->M+2);
            E_left[n][j-1] = E[k];
            F_left[n][j-1] = l2_norm(F[k]);
            T_left[n][j-1] = T[k];

            k = cell_id(mesh->N, j, mesh->N+2, mesh->M+2);
            E_right[n][j-1] = E[k];
            F_right[n][j-1] = l2_norm(F[k]);
            T_right[n][j-1] = T[k];
        }

        /* *************** etape 1 ******************/
        phase_1();
        // phase_1_eq();

        /* Remplissage des mailles fantomes */
        for (int i = 1; i < mesh->N+1; i++){
            double x_i = mesh->x[i];
            int k = cell_id(i, mesh->M+1, mesh->N+2, mesh->M+2);
            E[k] = E_u(t, x_i);
            F[k] = F_u(t, x_i);
            T[k] = T_u(t, x_i);

            k = cell_id(i, 0, mesh->N+2, mesh->M+2);
            E[k] = E_d(t, x_i);
            F[k] = F_d(t, x_i);
            T[k] = T_d(t, x_i);
        }
        for (int j = 1; j < mesh->M+1; j++){
            double y_j = mesh->y[j];
            int k = cell_id(0, j, mesh->N+2, mesh->M+2);
            E[k] = E_l(t, y_j);
            F[k] = F_l(t, y_j);
            T[k] = T_l(t, y_j);

            k = cell_id(mesh->N+1, j, mesh->N+2, mesh->M+2);
            E[k] = E_r(t, y_j);
            F[k] = F_r(t, y_j);
            T[k] = T_r(t, y_j);
        }


        /* *************** etape 2 ******************/
        phase_2();

        time_steps[n] = t;
        t += dt;
        n += 1;
    }
};


void Solver::display(){
    cout << "-----------  E  -----------\n" ;
    for (int j = mesh->M; j > 0; j--){
        for (int i = 1; i < mesh->N+1; i++){
            int k = cell_id(i, j, mesh->N+2, mesh->M+2);
            cout << E[k] << "  ";
        }
        cout << "\n";
    }

    // cout << "-----------  F  -----------\n" ;
    // for (int j = mesh->M; j > 0; j--){
    //     for (int i = 1; i < mesh->N+1; i++){
    //         int k = cell_id(i, j, mesh->N+2, mesh->M+2);
    //         cout << l2_norm(F[k]) << "  ";
    //     }
    //     cout << "\n";
    // }

    // cout << "-----------  T  -----------\n" ;
    // for (int j = mesh->M; j > 0; j--){
    //     for (int i = 1; i < mesh->N+1; i++){
    //         int k = cell_id(i, j, mesh->N+2, mesh->M+2);
    //         cout << T[k] << "  ";
    //     }
    //     cout << "\n";
    // }

    cout << "\n";
};

Solver::~Solver(){
    free(E_up, step_count);
    free(F_up, step_count);
    free(T_up, step_count);

    free(E_down, step_count);
    free(F_down, step_count);
    free(T_down, step_count);

    free(E_left, step_count);
    free(F_left, step_count);
    free(T_left, step_count);

    free(E_right, step_count);
    free(F_right, step_count);
    free(T_right, step_count);

    if (rho_expr.compare("custom") == 0)
        free(attr, n_niche);
};
