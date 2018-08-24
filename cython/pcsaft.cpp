#include <vector>
#include <string>
#include <cmath>
#include "math.h"
#include <iostream>
#include <Eigen/Dense>

#include "pcsaft.h"

using namespace std;
using namespace Eigen;

vector<double> XA_find(vector<double> XA_guess, int ncomp, vector<double> delta_ij, double den,
    vector<double> x) {
    /**Iterate over this function in order to solve for XA*/
    int n_sites = XA_guess.size()/ncomp;
    double summ2;
    vector<double> XA = XA_guess;

    for (int i = 0; i < ncomp; i++) {
        for (int kout = 0; kout < n_sites; kout++) {
            summ2 = 0.;
            for (int j = 0; j < ncomp; j++) {
                for (int kin = 0; kin < n_sites; kin++) {
                    if (kin != kout) {
                        summ2 += den*x[j]*XA_guess[j*ncomp+kin]*delta_ij[i*ncomp+j];
                    }
                }
            }
            XA[i*ncomp+kout] = 1./(1.+summ2);
        }
    }

    return XA;
    }

vector<double> dXA_find(int ncA, int ncomp, vector<int> iA, vector<double> delta_ij, 
    double den, vector<double> XA, vector<double> ddelta_dd, vector<double> x, int n_sites) {
    /**Solve for the derivative of XA with respect to density.*/
    MatrixXd B(n_sites*ncA*ncomp, 1);
    MatrixXd A = MatrixXd::Zero(n_sites*ncA*ncomp, n_sites*ncA*ncomp);

    double sum1, sum2;
    int indx1, indx2;
    int indx4 = -1;
    int indx3 = -1;
    for (int i = 0; i < ncomp; i++) {    
        indx1 = -1;
        if (find(iA.begin(), iA.end(), i) != iA.end()) {
            indx4 += 1;
        }
        for (int j = 0; j < ncA; j++) {
            for (int h = 0; h < n_sites; h++) {
                indx1 += 1;
                indx3 += 1;
                indx2 = -1;
                sum1 = 0;
                for (int k = 0; k < ncA; k++) {
                    for (int l = 0; l < n_sites; l++) {
                        indx2 += 1;
                        sum1 = sum1 + den*x[k]*(XA[indx2]*ddelta_dd[j*(ncA+ncA+ncomp)+k*(ncA+ncomp)+i]*((indx1+indx2)%2)); // (indx1+indx2)%2 ensures that A-A and B-B associations are set to zero
                        A(indx1+i*n_sites*ncA,indx2+i*n_sites*ncA) =
                        A(indx1+i*n_sites*ncA,indx2+i*n_sites*ncA) +
                        XA[indx1]*XA[indx1]*den*x[k]*delta_ij[j*ncA+k]*((indx1+indx2)%2);
                    }
                }

                sum2 = 0;
                if (find(iA.begin(), iA.end(), i) != iA.end()) {
                    for (int k = 0; k < n_sites; k++) {
                        sum2 = sum2 + XA[n_sites*(indx4)+k]*delta_ij[indx4*ncA+j]*((indx1+k)%2);
                    }
                }

                A(indx3,indx3) = A(indx3,indx3) + 1;
                B(indx3) = -1*XA[indx1]*XA[indx1]*(sum1 + sum2);
            }
        }
    }

    MatrixXd solution = A.lu().solve(B); //Solves linear system of equations
    vector<double> dXA_dd(n_sites*ncA*ncomp);
    for (int i = 0; i < n_sites*ncA*ncomp; i++) {
        dXA_dd[i] = solution(i);
    }    
    return dXA_dd;
}


double pcsaft_Z_cpp(vector<double> x, vector<double> m, vector<double> s, vector<double> e,
    double t, double rho, add_args &cppargs) {
    /**
    Calculate the compressibility factor.

    Parameters
    ----------
    x : vector<double>, shape (n,)
        Mole fractions of each component. It has a length of n, where n is
        the number of components in the system.
    m : vector<double>, shape (n,)
        Segment number for each component.
    s : vector<double>, shape (n,)
        Segment diameter for each component. For ions this is the diameter of
        the hydrated ion. Units of Angstrom.
    e : vector<double>, shape (n,)
        Dispersion energy of each component. For ions this is the dispersion
        energy of the hydrated ion. Units of K.
    t : double
        Temperature (K)
    rho : double
        Molar density (mol m^{-3})
    cppargs : add_args
        A struct containing additional arguments that can be passed for 
        use in PC-SAFT:
        
        k_ij : vector<double>, shape (n*n,)
            Binary interaction parameters between components in the mixture. 
            (dimensions: ncomp x ncomp)
        e_assoc : vector<double>, shape (n,)
            Association energy of the associating components. For non associating
            compounds this is set to 0. Units of K.
        vol_a : vector<double>, shape (n,)
            Effective association volume of the associating components. For non 
            associating compounds this is set to 0.
        dipm : vector<double>, shape (n,)
            Dipole moment of the polar components. For components where the dipole 
            term is not used this is set to 0. Units of Debye.
        dip_num : vector<double>, shape (n,)
            The effective number of dipole functional groups on each component 
            molecule. Some implementations use this as an adjustable parameter 
            that is fit to data.
        z : vector<double>, shape (n,)
            Charge number of the ions          
        dielc : double
            Dielectric constant of the medium to be used for electrolyte
            calculations.

    Returns
    -------
    Z : double
        Compressibility factor
    */
    int ncomp = x.size(); // number of components
    vector<double> d (ncomp);
    for (int i = 0; i < ncomp; i++) {
        d[i] = s[i]*(1-0.12*exp(-3*e[i]/t));
    }
    if (!cppargs.z.empty()) {
        for (int i = 0; i < ncomp; i++) {
            if (cppargs.z[i] != 0) {      
                d[i] = s[i]*(1-0.12); // for ions the diameter is assumed to be temperature independent (see Held et al. 2014)
            }  
        }
    }

    double den = rho*N_AV/1.0e30;

    vector<double> zeta (4, 0);
    double summ;
    for (int i = 0; i < 4; i++) {
        summ = 0;
        for (int j = 0; j < ncomp; j++) {
            summ += x[j]*m[j]*pow(d[j], i);
        }
        zeta[i] = PI/6*den*summ;
    }

    double eta = zeta[3];
    double m_avg = 0;
    for (int i = 0; i < ncomp; i++) {
        m_avg += x[i]*m[i];
    }

    vector<double> ghs (ncomp, 0);
    vector<double> denghs (ncomp, 0);
    vector<double> e_ij (ncomp*ncomp, 0);
    vector<double> s_ij (ncomp*ncomp, 0);
    double m2es3 = 0.;
    double m2e2s3 = 0.;
    int idx = -1;
    for (int i = 0; i < ncomp; i++) {
        for (int j = 0; j < ncomp; j++) {
            idx += 1;
            s_ij[idx] = (s[i] + s[j])/2.;
            if (!cppargs.z.empty()) {
                if (cppargs.z[i]*cppargs.z[j] <= 0) { // for two cations or two anions e_ij is kept at zero to avoid dispersion between like ions (see Held et al. 2014)
                    if (cppargs.k_ij.empty()) {
                        e_ij[idx] = sqrt(e[i]*e[j]);
                    }                    
                    else {
                        e_ij[idx] = sqrt(e[i]*e[j])*(1-cppargs.k_ij[idx]);
                    }
                }
            } else {
                if (cppargs.k_ij.empty()) {
                    e_ij[idx] = sqrt(e[i]*e[j]);
                }                    
                else {
                    e_ij[idx] = sqrt(e[i]*e[j])*(1-cppargs.k_ij[idx]);
                }
            }
            m2es3 = m2es3 + x[i]*x[j]*m[i]*m[j]*e_ij[idx]/t*pow(s_ij[idx], 3);
            m2e2s3 = m2e2s3 + x[i]*x[j]*m[i]*m[j]*pow(e_ij[idx]/t,2)*pow(s_ij[idx], 3);
        }
        ghs[i] = 1/(1-zeta[3]) + (d[i]*d[i]/(d[i]+d[i]))*3*zeta[2]/(1-zeta[3])/(1-zeta[3]) + 
            pow(d[i]*d[i]/(d[i]+d[i]), 2)*2*zeta[2]*zeta[2]/pow(1-zeta[3], 3);
        denghs[i] = zeta[3]/(1-zeta[3])/(1-zeta[3]) + 
            (d[i]*d[i]/(d[i]+d[i]))*(3*zeta[2]/(1-zeta[3])/(1-zeta[3]) +
            6*zeta[2]*zeta[3]/pow(1-zeta[3], 3)) + 
            pow(d[i]*d[i]/(d[i]+d[i]), 2)*(4*zeta[2]*zeta[2]/pow(1-zeta[3], 3) +
            6*zeta[2]*zeta[2]*zeta[3]/pow(1-zeta[3], 4));
    }

    double Zhs = zeta[3]/(1-zeta[3]) + 3.*zeta[1]*zeta[2]/zeta[0]/(1.-zeta[3])/(1.-zeta[3]) + 
        (3.*pow(zeta[2], 3.) - zeta[3]*pow(zeta[2], 3.))/zeta[0]/pow(1.-zeta[3], 3.);

    static double a0[7] = { 0.910563145, 0.636128145, 2.686134789, -26.54736249, 97.75920878, -159.5915409, 91.29777408 };
    static double a1[7] = { -0.308401692, 0.186053116, -2.503004726, 21.41979363, -65.25588533, 83.31868048, -33.74692293 };
    static double a2[7] = { -0.090614835, 0.452784281, 0.596270073, -1.724182913, -4.130211253, 13.77663187, -8.672847037 };
    static double b0[7] = { 0.724094694, 2.238279186, -4.002584949, -21.00357682, 26.85564136, 206.5513384, -355.6023561 };
    static double b1[7] = { -0.575549808, 0.699509552, 3.892567339, -17.21547165, 192.6722645, -161.8264617, -165.2076935 };
    static double b2[7] = { 0.097688312, -0.255757498, -9.155856153, 20.64207597, -38.80443005, 93.62677408, -29.66690559 };

    vector<double> a (7, 0);
    vector<double> b (7, 0);
    for (int i = 0; i < 7; i++) {
        a[i] = a0[i] + (m_avg-1.)/m_avg*a1[i] + (m_avg-1.)/m_avg*(m_avg-2.)/m_avg*a2[i];
        b[i] = b0[i] + (m_avg-1.)/m_avg*b1[i] + (m_avg-1.)/m_avg*(m_avg-2.)/m_avg*b2[i];
    }

    double detI1_det = 0.0;
    double detI2_det = 0.0;
    double I2 = 0.0;
    for (int i = 0; i < 7; i++) {
        detI1_det += a[i]*(i+1)*pow(eta, i);
        detI2_det += b[i]*(i+1)*pow(eta, i);
        I2 += b[i]*pow(eta, i);
    }
    double C1 = 1./(1. + m_avg*(8*eta-2*eta*eta)/pow(1-eta, 4) + (1-m_avg)*(20*eta-27*eta*eta+12*pow(eta, 3)-2*pow(eta, 4))/pow((1-eta)*(2-eta), 2.0));
    double C2 = -1.*C1*C1*(m_avg*(-4*eta*eta+20*eta+8)/pow(1-eta, 5) + (1-m_avg)*(2*pow(eta, 3)+12*eta*eta-48*eta+40)/pow((1-eta)*(2-eta), 3.0));

    summ = 0.0;
    for (int i = 0; i < ncomp; i++) {
        summ += x[i]*(m[i]-1)/ghs[i]*denghs[i];
    }

    double Zid = 1.0;
    double Zhc = m_avg*Zhs - summ;
    double Zdisp = -2*PI*den*detI1_det*m2es3 - PI*den*m_avg*(C1*detI2_det + C2*eta*I2)*m2e2s3;

    // Dipole term (Gross and Vrabec term) --------------------------------------
    double Zpolar = 0;
    if (!cppargs.dipm.empty()) {
        double A2 = 0.;
        double A3 = 0.;
        double dA2_det = 0.;
        double dA3_det = 0.;
        vector<double> adip (5, 0);
        vector<double> bdip (5, 0);
        vector<double> cdip (5, 0);
        vector<double> dipmSQ (ncomp, 0);
        double J2, dJ2_det, J3, dJ3_det;

        static double a0dip[5] = { 0.3043504, -0.1358588, 1.4493329, 0.3556977, -2.0653308 };
        static double a1dip[5] = { 0.9534641, -1.8396383, 2.0131180, -7.3724958, 8.2374135 };
        static double a2dip[5] = { -1.1610080, 4.5258607, 0.9751222, -12.281038, 5.9397575 };
        static double b0dip[5] = { 0.2187939, -1.1896431, 1.1626889, 0, 0 };
        static double b1dip[5] = { -0.5873164, 1.2489132, -0.5085280, 0, 0 };
        static double b2dip[5] = { 3.4869576, -14.915974, 15.372022, 0, 0 };
        static double c0dip[5] = { -0.0646774, 0.1975882, -0.8087562, 0.6902849, 0 };
        static double c1dip[5] = { -0.9520876, 2.9924258, -2.3802636, -0.2701261, 0 };
        static double c2dip[5] = { -0.6260979, 1.2924686, 1.6542783, -3.4396744, 0 };

        const static double conv = 7242.702976750923; // conversion factor, see the note below Table 2 in Gross and Vrabec 2006

        for (int i = 0; i < ncomp; i++) {     
            dipmSQ[i] = pow(cppargs.dipm[i], 2.)/(m[i]*e[i]*pow(s[i],3.))*conv;
        }

        double m_ij;
        for (int i = 0; i < ncomp; i++) {
            for (int j = 0; j < ncomp; j++) {
                m_ij = sqrt(m[i]*m[j]);
                if (m_ij > 2) {
                    m_ij = 2;
                }
                J2 = 0.;
                dJ2_det = 0.;
                for (int l = 0; l < 5; l++) {
                    adip[l] = a0dip[l] + (m_ij-1)/m_ij*a1dip[l] + (m_ij-1)/m_ij*(m_ij-2)/m_ij*a2dip[l];
                    bdip[l] = b0dip[l] + (m_ij-1)/m_ij*b1dip[l] + (m_ij-1)/m_ij*(m_ij-2)/m_ij*b2dip[l];
                    J2 += (adip[l] + bdip[l]*e_ij[j*ncomp+j]/t)*pow(eta, l); // j*ncomp+j needs to be used for e_ij because it is formatted as a 1D vector                   
                    dJ2_det += (adip[l] + bdip[l]*e_ij[j*ncomp+j]/t)*l*pow(eta, l-1);
                }
                A2 += x[i]*x[j]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3)/
                    pow(s_ij[i*ncomp+j],3)*cppargs.dip_num[i]*cppargs.dip_num[j]*dipmSQ[i]*dipmSQ[j]*J2;
                dA2_det += x[i]*x[j]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*pow(s_ij[i*ncomp+i],3)*
                    pow(s_ij[j*ncomp+j],3)/pow(s_ij[i*ncomp+j],3)*cppargs.dip_num[i]*cppargs.dip_num[j]*dipmSQ[i]*dipmSQ[j]*dJ2_det;
            }
        }

        double m_ijk;
        for (int i = 0; i < ncomp; i++) {
            for (int j = 0; j < ncomp; j++) {
                for (int k = 0; k < ncomp; k++) {
                    m_ijk = pow((m[i]*m[j]*m[k]),1/3.);
                    if (m_ijk > 2) {
                        m_ijk = 2;
                    }
                    J3 = 0.;
                    dJ3_det = 0.;
                    for (int l = 0; l < 5; l++) {
                        cdip[l] = c0dip[l] + (m_ijk-1)/m_ijk*c1dip[l] + (m_ijk-1)/m_ijk*(m_ijk-2)/m_ijk*c2dip[l];
                        J3 += cdip[l]*pow(eta, l);
                        dJ3_det += cdip[l]*l*pow(eta, (l-1));
                    }
                    A3 += x[i]*x[j]*x[k]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*e_ij[k*ncomp+k]/t*
                        pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3)*pow(s_ij[k*ncomp+k],3)/s_ij[i*ncomp+j]/s_ij[i*ncomp+k]/
                        s_ij[j*ncomp+k]*cppargs.dip_num[i]*cppargs.dip_num[j]*cppargs.dip_num[k]*dipmSQ[i]*
                        dipmSQ[j]*dipmSQ[k]*J3;
                    dA3_det += x[i]*x[j]*x[k]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*e_ij[k*ncomp+k]/t*
                        pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3)*pow(s_ij[k*ncomp+k],3)/s_ij[i*ncomp+j]/s_ij[i*ncomp+k]/
                        s_ij[j*ncomp+k]*cppargs.dip_num[i]*cppargs.dip_num[j]*cppargs.dip_num[k]*dipmSQ[i]*
                        dipmSQ[j]*dipmSQ[k]*dJ3_det;
                }
            }
        }

        A2 = -PI*den*A2;
        A3 = -4/3.*PI*PI*den*den*A3;
        dA2_det = -PI*den*dA2_det;
        dA3_det = -4/3.*PI*PI*den*den*dA3_det;

        Zpolar = eta*((dA2_det*(1-A3/A2)+(dA3_det*A2-A3*dA2_det)/A2)/(1-A3/A2)/(1-A3/A2));
    }

    // Association term -------------------------------------------------------
    // only the 2B association type is currently implemented
    double Zassoc = 0;
    if (!cppargs.e_assoc.empty()) {
        int a_sites = 2;
        int ncA = count_if(cppargs.e_assoc.begin(), cppargs.e_assoc.end(), IsNotZero); // number of associating compounds in the fluid
 
        vector<int> iA (ncA, 0); //indices of associating compounds
        int ctr = 0;
        for (int i = 0; i < ncomp; i++) {
            if (cppargs.e_assoc[i] != 0.0) {
                iA[ctr] = i;
                ctr += 1;
            }
        }

        vector<double> XA (ncA*a_sites, 0);
        vector<double> eABij (ncA*ncA, 0);
        vector<double> volABij (ncA*ncA, 0);
        vector<double> delta_ij (ncA*ncA, 0);
        vector<double> ddelta_dd (ncA*ncA*ncomp, 0);

        // these indices are necessary because we are only using 1D vectors
        int idxa = -1; // index over only associating compounds
        int idxi = 0; // index for the ii-th compound
        int idxj = 0; // index for the jj-th compound
        int idx_ddelta = -1; // index for ddelta_dd vector
        double dghsd_dd;
        for (int i = 0; i < ncA; i++) {
            idxi = iA[i]*ncomp+iA[i];
            for (int j = 0; j < ncA; j++) {
                idxa += 1;
                idxj = iA[j]*ncomp+iA[j];
                eABij[idxa] = (cppargs.e_assoc[iA[i]]+cppargs.e_assoc[iA[j]])/2.;
                volABij[idxa] = sqrt(cppargs.vol_a[iA[i]]*cppargs.vol_a[iA[j]])*pow(sqrt(s_ij[idxi]*
                    s_ij[idxj])/(0.5*(s_ij[idxi]+s_ij[idxj])), 3);
                delta_ij[idxa] = ghs[iA[j]]*(exp(eABij[idxa]/t)-1)*pow(s_ij[iA[i]*ncomp+iA[j]], 3)*volABij[idxa];
                for (int k = 0; k < ncomp; k++) {
                    idx_ddelta += 1;
                    dghsd_dd = PI/6.*m[k]*(pow(d[k], 3)/(1-zeta[3])/(1-zeta[3]) + 3*d[iA[i]]*d[iA[j]]/
                        (d[iA[i]]+d[iA[j]])*(d[k]*d[k]/(1-zeta[3])/(1-zeta[3])+2*pow(d[k], 3)*
                        zeta[2]/pow(1-zeta[3], 3)) + 2*pow((d[iA[i]]*d[iA[j]]/(d[iA[i]]+d[iA[j]])), 2)*
                        (2*d[k]*d[k]*zeta[2]/pow(1-zeta[3], 3)+3*(pow(d[k], 3)*zeta[2]*zeta[2]
                        /pow(1-zeta[3], 4))));
                    ddelta_dd[idx_ddelta] = dghsd_dd*(exp(eABij[idxa]/t)-1)*pow(s_ij[iA[i]*ncomp+iA[j]], 3)*volABij[idxa];
                }
            }           
            XA[i*2] = (-1 + sqrt(1+8*den*delta_ij[i*ncA+i]))/(4*den*delta_ij[i*ncA+i]);
            XA[i*2+1] = XA[i*2];
        }

        vector<double> x_assoc(ncA); // mole fractions of only the associating compounds
        for (int i = 0; i < ncA; i++) {
            x_assoc[i] = x[iA[i]];
        }

        ctr = 0;
        double dif = 1000.;
        vector<double> XA_old = XA;
        while ((ctr < 500) && (dif > 1e-9)) {
            ctr += 1;
            XA = XA_find(XA, ncA, delta_ij, den, x_assoc);
            dif = 0.;
            for (int i = 0; i < ncA*2; i++) {
                dif += abs(XA[i] - XA_old[i]);
            }
            XA_old = XA;
        }

        vector<double> dXA_dd(ncA*ncA*ncomp, 0);
        dXA_dd = dXA_find(ncA, ncomp, iA, delta_ij, den, XA, ddelta_dd, x_assoc, a_sites);

        summ = 0.;
        for (int i = 0; i < ncomp; i++) {
            for (int j = 0; j < ncA; j++) {
                for (int k = 0; k < a_sites; k++) {
                    summ += x[i]*den*x[iA[j]]*(1/XA[(j+1)*k]-0.5)*dXA_dd[i*(ncA*a_sites)+j*(a_sites)+k];
                }
            }
        }

        Zassoc = summ;
    }

    // Ion term ---------------------------------------------------------------
    double Zion = 0;
    if (!cppargs.z.empty()) {
        vector<double> q(cppargs.z.begin(), cppargs.z.end());
        for (int i = 0; i < ncomp; i++) {
            q[i] = q[i]*E_CHRG;
        }

        summ = 0.;
        for (int i = 0; i < ncomp; i++) {
            summ += cppargs.z[i]*cppargs.z[i]*x[i];
        }
        
        double kappa = sqrt(den*E_CHRG*E_CHRG/kb/t/(cppargs.dielc*perm_vac)*summ); // the inverse Debye screening length. Equation 4 in Held et al. 2008.

        if (kappa != 0) {
            double chi, sigma_k;
            summ = 0.;
            for (int i = 0; i < ncomp; i++) {
                chi = 3/pow(kappa*s[i], 3)*(1.5 + log(1+kappa*s[i]) - 2*(1+kappa*s[i]) +
                    0.5*pow(1+kappa*s[i], 2));
                sigma_k = -2*chi+3/(1+kappa*s[i]);
                summ += q[i]*q[i]*x[i]*sigma_k;
            }
            Zion = -1*kappa/24./PI/kb/t/(cppargs.dielc*perm_vac)*summ;
        }
    }

    double Z = Zid + Zhc + Zdisp + Zpolar + Zassoc + Zion;
    return Z;
}


vector<double> pcsaft_fugcoef_cpp(vector<double> x, vector<double> m, vector<double> s, vector<double> e,
    double t, double rho, add_args &cppargs) {
    /**
    Calculate the fugacity coefficients for one phase of the system.

    Parameters
    ----------
    x : vector, shape (n,)
        Mole fractions of each component. It has a length of n, where n is
        the number of components in the system.
    m : vector<double>, shape (n,)
        Segment number for each component.
    s : vector<double>, shape (n,)
        Segment diameter for each component. For ions this is the diameter of
        the hydrated ion. Units of Angstrom.
    e : vector<double>, shape (n,)
        Dispersion energy of each component. For ions this is the dispersion
        energy of the hydrated ion. Units of K.
    t : double
        Temperature (K)
    rho : double
        Molar density (mol m^{-3})
    cppargs : add_args
        A struct containing additional arguments that can be passed for 
        use in PC-SAFT:
        
        k_ij : vector<double>, shape (n*n,)
            Binary interaction parameters between components in the mixture. 
            (dimensions: ncomp x ncomp)
        e_assoc : vector<double>, shape (n,)
            Association energy of the associating components. For non associating
            compounds this is set to 0. Units of K.
        vol_a : vector<double>, shape (n,)
            Effective association volume of the associating components. For non 
            associating compounds this is set to 0.
        dipm : vector<double>, shape (n,)
            Dipole moment of the polar components. For components where the dipole 
            term is not used this is set to 0. Units of Debye.
        dip_num : vector<double>, shape (n,)
            The effective number of dipole functional groups on each component 
            molecule. Some implementations use this as an adjustable parameter 
            that is fit to data.
        z : vector<double>, shape (n,)
            Charge number of the ions          
        dielc : double
            Dielectric constant of the medium to be used for electrolyte
            calculations.

    Returns
    -------
    fugcoef : vector<double>, shape (n,)
        Fugacity coefficients of each component.
    */
    int ncomp = x.size(); // number of components
    vector<double> d (ncomp);
    for (int i = 0; i < ncomp; i++) {
        d[i] = s[i]*(1-0.12*exp(-3*e[i]/t));
    }
    if (!cppargs.z.empty()) {
        for (int i = 0; i < ncomp; i++) {
            if (cppargs.z[i] != 0) {      
                d[i] = s[i]*(1-0.12); // for ions the diameter is assumed to be temperature independent (see Held et al. 2014)
            }  
        }
    }

    double den = rho*N_AV/1.0e30;

    vector<double> zeta (4, 0);
    double summ;
    for (int i = 0; i < 4; i++) {
        summ = 0;
        for (int j = 0; j < ncomp; j++) {
            summ += x[j]*m[j]*pow(d[j], i);
        }
        zeta[i] = PI/6*den*summ;
    }

    double eta = zeta[3];
    double m_avg = 0;
    for (int i = 0; i < ncomp; i++) {
        m_avg += x[i]*m[i];
    }

    vector<double> ghs(ncomp, 0);
    vector<double> denghs(ncomp, 0);
    vector<double> e_ij(ncomp*ncomp, 0);
    vector<double> s_ij(ncomp*ncomp, 0);
    double m2es3 = 0.;
    double m2e2s3 = 0.;
    int idx = -1;
    for (int i = 0; i < ncomp; i++) {
        for (int j = 0; j < ncomp; j++) {
            idx += 1;
            s_ij[idx] = (s[i] + s[j])/2.;
            if (!cppargs.z.empty()) {
                if (cppargs.z[i]*cppargs.z[j] <= 0) { // for two cations or two anions e_ij is kept at zero to avoid dispersion between like ions (see Held et al. 2014)
                    if (cppargs.k_ij.empty()) {
                        e_ij[idx] = sqrt(e[i]*e[j]);
                    }                    
                    else {
                        e_ij[idx] = sqrt(e[i]*e[j])*(1-cppargs.k_ij[idx]);
                    }
                }
            } else {
                if (cppargs.k_ij.empty()) {
                    e_ij[idx] = sqrt(e[i]*e[j]);
                }                    
                else {
                    e_ij[idx] = sqrt(e[i]*e[j])*(1-cppargs.k_ij[idx]);
                }
            }
            m2es3 = m2es3 + x[i]*x[j]*m[i]*m[j]*e_ij[idx]/t*pow(s_ij[idx], 3);
            m2e2s3 = m2e2s3 + x[i]*x[j]*m[i]*m[j]*pow(e_ij[idx]/t,2)*pow(s_ij[idx], 3);
        }
        ghs[i] = 1/(1-zeta[3]) + (d[i]*d[i]/(d[i]+d[i]))*3*zeta[2]/(1-zeta[3])/(1-zeta[3]) + 
            pow(d[i]*d[i]/(d[i]+d[i]), 2)*2*zeta[2]*zeta[2]/pow(1-zeta[3], 3);
        denghs[i] = zeta[3]/(1-zeta[3])/(1-zeta[3]) + 
            (d[i]*d[i]/(d[i]+d[i]))*(3*zeta[2]/(1-zeta[3])/(1-zeta[3]) +
            6*zeta[2]*zeta[3]/pow(1-zeta[3], 3)) + 
            pow(d[i]*d[i]/(d[i]+d[i]), 2)*(4*zeta[2]*zeta[2]/pow(1-zeta[3], 3) +
            6*zeta[2]*zeta[2]*zeta[3]/pow(1-zeta[3], 4));
    }

    double ares_hs = 1/zeta[0]*(3*zeta[1]*zeta[2]/(1-zeta[3]) + pow(zeta[2], 3.)/(zeta[3]*pow(1-zeta[3],2)) 
            + (pow(zeta[2], 3.)/pow(zeta[3], 2.) - zeta[0])*log(1-zeta[3]));
    double Zhs = zeta[3]/(1-zeta[3]) + 3.*zeta[1]*zeta[2]/zeta[0]/(1.-zeta[3])/(1.-zeta[3]) + 
        (3.*pow(zeta[2], 3.) - zeta[3]*pow(zeta[2], 3.))/zeta[0]/pow(1.-zeta[3], 3.);

    static double a0[7] = { 0.910563145, 0.636128145, 2.686134789, -26.54736249, 97.75920878, -159.5915409, 91.29777408 };
    static double a1[7] = { -0.308401692, 0.186053116, -2.503004726, 21.41979363, -65.25588533, 83.31868048, -33.74692293 };
    static double a2[7] = { -0.090614835, 0.452784281, 0.596270073, -1.724182913, -4.130211253, 13.77663187, -8.672847037 };
    static double b0[7] = { 0.724094694, 2.238279186, -4.002584949, -21.00357682, 26.85564136, 206.5513384, -355.6023561 };
    static double b1[7] = { -0.575549808, 0.699509552, 3.892567339, -17.21547165, 192.6722645, -161.8264617, -165.2076935 };
    static double b2[7] = { 0.097688312, -0.255757498, -9.155856153, 20.64207597, -38.80443005, 93.62677408, -29.66690559 };

    vector<double> a (7, 0);
    vector<double> b (7, 0);
    for (int i = 0; i < 7; i++) {
        a[i] = a0[i] + (m_avg-1.)/m_avg*a1[i] + (m_avg-1.)/m_avg*(m_avg-2.)/m_avg*a2[i];
        b[i] = b0[i] + (m_avg-1.)/m_avg*b1[i] + (m_avg-1.)/m_avg*(m_avg-2.)/m_avg*b2[i];
    }

    double detI1_det = 0.0;
    double detI2_det = 0.0;
    double I1 = 0.0;
    double I2 = 0.0;
    for (int i = 0; i < 7; i++) {
        detI1_det += a[i]*(i+1)*pow(eta, i);
        detI2_det += b[i]*(i+1)*pow(eta, i);
        I2 += b[i]*pow(eta, i);
        I1 += a[i]*pow(eta, i);
    }
    double C1 = 1./(1. + m_avg*(8*eta-2*eta*eta)/pow(1-eta, 4) + (1-m_avg)*(20*eta-27*eta*eta+12*pow(eta, 3)-2*pow(eta, 4))/pow((1-eta)*(2-eta), 2.0));
    double C2 = -1.*C1*C1*(m_avg*(-4*eta*eta+20*eta+8)/pow(1-eta, 5) + (1-m_avg)*(2*pow(eta, 3)+12*eta*eta-48*eta+40)/pow((1-eta)*(2-eta), 3.0));

    summ = 0.0;
    for (int i = 0; i < ncomp; i++) {
        summ += x[i]*(m[i]-1)*log(ghs[i]);
    }

    double ares_hc = m_avg*ares_hs - summ;
    double ares_disp = -2*PI*den*I1*m2es3 - PI*den*m_avg*C1*I2*m2e2s3;

    summ = 0.0;
    for (int i = 0; i < ncomp; i++) {
        summ += x[i]*(m[i]-1)/ghs[i]*denghs[i];
    }

    double Zhc = m_avg*Zhs - summ;
    double Zdisp = -2*PI*den*detI1_det*m2es3 - PI*den*m_avg*(C1*detI2_det + C2*eta*I2)*m2e2s3;

    vector<double> dghs_dx(ncomp*ncomp, 0);
    vector<double> dahs_dx(ncomp, 0);
    vector<double> dzeta_dx(4, 0);
    idx = -1;
    for (int i = 0; i < ncomp; i++) {
        for (int l = 0; l < 4; l++) {
            dzeta_dx[l] = PI/6.*den*m[i]*pow(d[i],l);
        }
        for (int j = 0; j < ncomp; j++) {
            idx += 1;
            dghs_dx[idx] = dzeta_dx[3]/(1-zeta[3])/(1-zeta[3]) + (d[j]*d[j]/(d[j]+d[j]))* 
                    (3*dzeta_dx[2]/(1-zeta[3])/(1-zeta[3]) + 6*zeta[2]*dzeta_dx[3]/pow(1-zeta[3],3)) 
                    + pow(d[j]*d[j]/(d[j]+d[j]),2)*(4*zeta[2]*dzeta_dx[2]/pow(1-zeta[3],3) 
                    + 6*zeta[2]*zeta[2]*dzeta_dx[3]/pow(1-zeta[3],4));
        }
        dahs_dx[i] = -dzeta_dx[0]/zeta[0]*ares_hs + 1/zeta[0]*(3*(dzeta_dx[1]*zeta[2] 
                + zeta[1]*dzeta_dx[2])/(1-zeta[3]) + 3*zeta[1]*zeta[2]*dzeta_dx[3] 
                /(1-zeta[3])/(1-zeta[3]) + 3*zeta[2]*zeta[2]*dzeta_dx[2]/zeta[3]/(1-zeta[3])/(1-zeta[3]) 
                + pow(zeta[2],3)*dzeta_dx[3]*(3*zeta[3]-1)/zeta[3]/zeta[3]/pow(1-zeta[3],3) 
                + log(1-zeta[3])*((3*zeta[2]*zeta[2]*dzeta_dx[2]*zeta[3] - 
                2*pow(zeta[2],3)*dzeta_dx[3])/pow(zeta[3],3) - dzeta_dx[0]) + 
                (zeta[0]-pow(zeta[2],3)/zeta[3]/zeta[3])*dzeta_dx[3]/(1-zeta[3]));
    }

    vector<double> dadisp_dx(ncomp, 0);
    vector<double> dahc_dx(ncomp, 0);
    double dzeta3_dx, daa_dx, db_dx, dI1_dx, dI2_dx, dm2es3_dx, dm2e2s3_dx, dC1_dx;
    for (int i = 0; i < ncomp; i++) {
        dzeta3_dx = PI/6.*den*m[i]*pow(d[i],3);
        dI1_dx = 0.0;
        dI2_dx = 0.0;
        dm2es3_dx = 0.0;
        dm2e2s3_dx = 0.0;
        for (int l = 0; l < 7; l++) {
            daa_dx = m[i]/m_avg/m_avg*a1[l] + m[i]/m_avg/m_avg*(3-4/m_avg)*a2[l];
            db_dx = m[i]/m_avg/m_avg*b1[l] + m[i]/m_avg/m_avg*(3-4/m_avg)*b2[l];
            dI1_dx += a[l]*l*dzeta3_dx*pow(eta,l-1) + daa_dx*pow(eta,l);
            dI2_dx += b[l]*l*dzeta3_dx*pow(eta,l-1) + db_dx*pow(eta,l);
        }
        for (int j = 0; j < ncomp; j++) {
            dm2es3_dx += x[j]*m[j]*(e_ij[i*ncomp+j]/t)*pow(s_ij[i*ncomp+j],3);
            dm2e2s3_dx += x[j]*m[j]*pow(e_ij[i*ncomp+j]/t,2)*pow(s_ij[i*ncomp+j],3);
            dahc_dx[i] += x[j]*(m[j]-1)/ghs[j]*dghs_dx[i*ncomp+j];
        }
        dm2es3_dx = dm2es3_dx*2*m[i];
        dm2e2s3_dx = dm2e2s3_dx*2*m[i];
        dahc_dx[i] = m[i]*ares_hs + m_avg*dahs_dx[i] - dahc_dx[i] - (m[i]-1)*log(ghs[i]);
        dC1_dx = C2*dzeta3_dx - C1*C1*(m[i]*(8*eta-2*eta*eta)/pow(1-eta,4) - 
            m[i]*(20*eta-27*eta*eta+12*pow(eta,3)-2*pow(eta,4))/pow((1-eta)*(2-eta),2));

        dadisp_dx[i] = -2*PI*den*(dI1_dx*m2es3 + I1*dm2es3_dx) - PI*den 
            *((m[i]*C1*I2 + m_avg*dC1_dx*I2 + m_avg*C1*dI2_dx)*m2e2s3 
            + m_avg*C1*I2*dm2e2s3_dx);
    }

    vector<double> mu_hc(ncomp, 0);
    vector<double> mu_disp(ncomp, 0);
    for (int i = 0; i < ncomp; i++) {
        for (int j = 0; j < ncomp; j++) {
            mu_hc[i] += x[j]*dahc_dx[j];
            mu_disp[i] += x[j]*dadisp_dx[j];
        }
        mu_hc[i] = ares_hc + Zhc + dahc_dx[i] - mu_hc[i];
        mu_disp[i] = ares_disp + Zdisp + dadisp_dx[i] - mu_disp[i];
    }

    // Dipole term (Gross and Vrabec term) --------------------------------------
    vector<double> mu_polar(ncomp, 0);
    if (!cppargs.dipm.empty()) {
        double A2 = 0.;
        double A3 = 0.;
        double dA2_det = 0.;
        double dA3_det = 0.;
        vector<double> dA2_dx(ncomp, 0);
        vector<double> dA3_dx(ncomp, 0);

        static double a0dip[5] = { 0.3043504, -0.1358588, 1.4493329, 0.3556977, -2.0653308 };
        static double a1dip[5] = { 0.9534641, -1.8396383, 2.0131180, -7.3724958, 8.2374135 };
        static double a2dip[5] = { -1.1610080, 4.5258607, 0.9751222, -12.281038, 5.9397575 };
        static double b0dip[5] = { 0.2187939, -1.1896431, 1.1626889, 0, 0 };
        static double b1dip[5] = { -0.5873164, 1.2489132, -0.5085280, 0, 0 };
        static double b2dip[5] = { 3.4869576, -14.915974, 15.372022, 0, 0 };
        static double c0dip[5] = { -0.0646774, 0.1975882, -0.8087562, 0.6902849, 0 };
        static double c1dip[5] = { -0.9520876, 2.9924258, -2.3802636, -0.2701261, 0 };
        static double c2dip[5] = { -0.6260979, 1.2924686, 1.6542783, -3.4396744, 0 };

        const static double conv = 7242.702976750923; // conversion factor, see the note below Table 2 in Gross and Vrabec 2006

        vector<double> dipmSQ (ncomp, 0);
        for (int i = 0; i < ncomp; i++) {     
            dipmSQ[i] = pow(cppargs.dipm[i], 2.)/(m[i]*e[i]*pow(s[i],3.))*conv;
        }

        vector<double> adip (5, 0);
        vector<double> bdip (5, 0);
        vector<double> cdip (5, 0);
        double J2, dJ2_det, J3, dJ3_det;
        double m_ij;
        double m_ijk;
        for (int i = 0; i < ncomp; i++) {
            for (int j = 0; j < ncomp; j++) {
                m_ij = sqrt(m[i]*m[j]);
                if (m_ij > 2) {
                    m_ij = 2;
                }
                J2 = 0.;
                dJ2_det = 0.;
                for (int l = 0; l < 5; l++) {
                    adip[l] = a0dip[l] + (m_ij-1)/m_ij*a1dip[l] + (m_ij-1)/m_ij*(m_ij-2)/m_ij*a2dip[l];
                    bdip[l] = b0dip[l] + (m_ij-1)/m_ij*b1dip[l] + (m_ij-1)/m_ij*(m_ij-2)/m_ij*b2dip[l];
                    J2 += (adip[l] + bdip[l]*e_ij[j*ncomp+j]/t)*pow(eta, l); // j*ncomp+j needs to be used for e_ij because it is formatted as a 1D vector                   
                    dJ2_det += (adip[l] + bdip[l]*e_ij[j*ncomp+j]/t)*l*pow(eta, l-1);
                }
                A2 += x[i]*x[j]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3)/
                    pow(s_ij[i*ncomp+j],3)*cppargs.dip_num[i]*cppargs.dip_num[j]*dipmSQ[i]*dipmSQ[j]*J2;
                dA2_det += x[i]*x[j]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*pow(s_ij[i*ncomp+i],3)*
                    pow(s_ij[j*ncomp+j],3)/pow(s_ij[i*ncomp+j],3)*cppargs.dip_num[i]*cppargs.dip_num[j]*dipmSQ[i]*dipmSQ[j]*dJ2_det;
                if (i == j) {
                    dA2_dx[i] += e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3) 
                        /pow(s_ij[i*ncomp+j],3)*cppargs.dip_num[i]*cppargs.dip_num[j]*dipmSQ[i]*dipmSQ[j]* 
                        (x[i]*x[j]*dJ2_det*PI/6.*den*m[i]*pow(d[i],3) + 2*x[j]*J2);
                }
                else {
                    dA2_dx[i] += e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3) 
                        /pow(s_ij[i*ncomp+j],3)*cppargs.dip_num[i]*cppargs.dip_num[j]*dipmSQ[i]*dipmSQ[j]* 
                        (x[i]*x[j]*dJ2_det*PI/6.*den*m[i]*pow(d[i],3) + x[j]*J2);
                }

                for (int k = 0; k < ncomp; k++) {
                    m_ijk = pow((m[i]*m[j]*m[k]),1/3.);
                    if (m_ijk > 2) {
                        m_ijk = 2;
                    }
                    J3 = 0.;
                    dJ3_det = 0.;
                    for (int l = 0; l < 5; l++) {
                        cdip[l] = c0dip[l] + (m_ijk-1)/m_ijk*c1dip[l] + (m_ijk-1)/m_ijk*(m_ijk-2)/m_ijk*c2dip[l];
                        J3 += cdip[l]*pow(eta, l);
                        dJ3_det += cdip[l]*l*pow(eta, (l-1));
                    }
                    A3 += x[i]*x[j]*x[k]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*e_ij[k*ncomp+k]/t*
                        pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3)*pow(s_ij[k*ncomp+k],3)/s_ij[i*ncomp+j]/s_ij[i*ncomp+k]/
                        s_ij[j*ncomp+k]*cppargs.dip_num[i]*cppargs.dip_num[j]*cppargs.dip_num[k]*dipmSQ[i]*
                        dipmSQ[j]*dipmSQ[k]*J3;
                    dA3_det += x[i]*x[j]*x[k]*e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*e_ij[k*ncomp+k]/t*
                        pow(s_ij[i*ncomp+i],3)*pow(s_ij[j*ncomp+j],3)*pow(s_ij[k*ncomp+k],3)/s_ij[i*ncomp+j]/s_ij[i*ncomp+k]/
                        s_ij[j*ncomp+k]*cppargs.dip_num[i]*cppargs.dip_num[j]*cppargs.dip_num[k]*dipmSQ[i]*
                        dipmSQ[j]*dipmSQ[k]*dJ3_det;
                    if ((i == j) && (i == k)) {
                        dA3_dx[i] += e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*e_ij[k*ncomp+k]/t*pow(s_ij[i*ncomp+i],3) 
                            *pow(s_ij[j*ncomp+j],3)*pow(s_ij[k*ncomp+k],3)/s_ij[i*ncomp+j]/s_ij[i*ncomp+k]/s_ij[j*ncomp+k] 
                            *cppargs.dip_num[i]*cppargs.dip_num[j]*cppargs.dip_num[k]*dipmSQ[i]*dipmSQ[j] 
                            *dipmSQ[k]*(x[i]*x[j]*x[k]*dJ3_det*PI/6.*den*m[i]*pow(d[i],3) 
                            + 3*x[j]*x[k]*J3);
                    }
                    else if ((i == j) || (i == k)) {
                        dA3_dx[i] += e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*e_ij[k*ncomp+k]/t*pow(s_ij[i*ncomp+i],3) 
                            *pow(s_ij[j*ncomp+j],3)*pow(s_ij[k*ncomp+k],3)/s_ij[i*ncomp+j]/s_ij[i*ncomp+k]/s_ij[j*ncomp+k] 
                            *cppargs.dip_num[i]*cppargs.dip_num[j]*cppargs.dip_num[k]*dipmSQ[i]*dipmSQ[j] 
                            *dipmSQ[k]*(x[i]*x[j]*x[k]*dJ3_det*PI/6.*den*m[i]*pow(d[i],3) 
                            + 2*x[j]*x[k]*J3);
                    }
                    else {
                        dA3_dx[i] += e_ij[i*ncomp+i]/t*e_ij[j*ncomp+j]/t*e_ij[k*ncomp+k]/t*pow(s_ij[i*ncomp+i],3) 
                            *pow(s_ij[j*ncomp+j],3)*pow(s_ij[k*ncomp+k],3)/s_ij[i*ncomp+j]/s_ij[i*ncomp+k]/s_ij[j*ncomp+k] 
                            *cppargs.dip_num[i]*cppargs.dip_num[j]*cppargs.dip_num[k]*dipmSQ[i]*dipmSQ[j] 
                            *dipmSQ[k]*(x[i]*x[j]*x[k]*dJ3_det*PI/6.*den*m[i]*pow(d[i],3) 
                            + x[j]*x[k]*J3);
                    }
                }
            }
        }

        A2 = -PI*den*A2;
        A3 = -4/3.*PI*PI*den*den*A3;
        dA2_det = -PI*den*dA2_det;
        dA3_det = -4/3.*PI*PI*den*den*dA3_det;
        for (int i = 0; i < ncomp; i++) {
            dA2_dx[i] = -PI*den*dA2_dx[i];
            dA3_dx[i] = -4/3.*PI*PI*den*den*dA3_dx[i];
        }

        vector<double> dapolar_dx(ncomp);
        for (int i = 0; i < ncomp; i++) {
            dapolar_dx[i] = (dA2_dx[i]*(1-A3/A2) + (dA3_dx[i]*A2 - A3*dA2_dx[i])/A2)/pow(1-A3/A2,2);
        }

        double ares_polar = A2/(1-A3/A2);
        double Zpolar = eta*((dA2_det*(1-A3/A2)+(dA3_det*A2-A3*dA2_det)/A2)/(1-A3/A2)/(1-A3/A2));
        for (int i = 0; i < ncomp; i++) {
            for (int j = 0; j < ncomp; j++) {
                mu_polar[i] += x[j]*dapolar_dx[j];
            }
            mu_polar[i] = ares_polar + Zpolar + dapolar_dx[i] - mu_polar[i];
        }
    }

    // Association term -------------------------------------------------------
    // only the 2B association type is currently implemented
    vector<double> mu_assoc(ncomp, 0);
    if (!cppargs.e_assoc.empty()) {
        int a_sites = 2;
        int ncA = count_if(cppargs.e_assoc.begin(), cppargs.e_assoc.end(), IsNotZero); // number of associating compounds in the fluid
 
        vector<int> iA (ncA, 0); //indices of associating compounds
        int ctr = 0;
        for (int i = 0; i < ncomp; i++) {
            if (cppargs.e_assoc[i] != 0.0) {
                iA[ctr] = i;
                ctr += 1;
            }
        }

        vector<double> XA (ncA*a_sites, 0);
        vector<double> eABij (ncA*ncA, 0);
        vector<double> volABij (ncA*ncA, 0);
        vector<double> delta_ij (ncA*ncA, 0);
        vector<double> ddelta_dd (ncA*ncA*ncomp, 0);

        // these indices are necessary because we are only using 1D vectors
        int idxa = -1; // index over only associating compounds
        int idxi = 0; // index for the ii-th compound
        int idxj = 0; // index for the jj-th compound
        int idx_ddelta = -1; // index for ddelta_dd vector
        double dghsd_dd;
        for (int i = 0; i < ncA; i++) {
            idxi = iA[i]*ncomp+iA[i];
            for (int j = 0; j < ncA; j++) {
                idxa += 1;
                idxj = iA[j]*ncomp+iA[j];
                eABij[idxa] = (cppargs.e_assoc[iA[i]]+cppargs.e_assoc[iA[j]])/2.;
                volABij[idxa] = sqrt(cppargs.vol_a[iA[i]]*cppargs.vol_a[iA[j]])*pow(sqrt(s_ij[idxi]*
                    s_ij[idxj])/(0.5*(s_ij[idxi]+s_ij[idxj])), 3);
                delta_ij[idxa] = ghs[iA[j]]*(exp(eABij[idxa]/t)-1)*pow(s_ij[iA[i]*ncomp+iA[j]], 3)*volABij[idxa];
                for (int k = 0; k < ncomp; k++) {
                    idx_ddelta += 1;
                    dghsd_dd = PI/6.*m[k]*(pow(d[k], 3)/(1-zeta[3])/(1-zeta[3]) + 3*d[iA[i]]*d[iA[j]]/
                        (d[iA[i]]+d[iA[j]])*(d[k]*d[k]/(1-zeta[3])/(1-zeta[3])+2*pow(d[k], 3)*
                        zeta[2]/pow(1-zeta[3], 3)) + 2*pow((d[iA[i]]*d[iA[j]]/(d[iA[i]]+d[iA[j]])), 2)*
                        (2*d[k]*d[k]*zeta[2]/pow(1-zeta[3], 3)+3*(pow(d[k], 3)*zeta[2]*zeta[2]
                        /pow(1-zeta[3], 4))));
                    ddelta_dd[idx_ddelta] = dghsd_dd*(exp(eABij[idxa]/t)-1)*pow(s_ij[iA[i]*ncomp+iA[j]], 3)*volABij[idxa];
                }
            }           
            XA[i*2] = (-1 + sqrt(1+8*den*delta_ij[i*ncA+i]))/(4*den*delta_ij[i*ncA+i]);
            XA[i*2+1] = XA[i*2];
        }

        vector<double> x_assoc(ncA); // mole fractions of only the associating compounds
        for (int i = 0; i < ncA; i++) {
            x_assoc[i] = x[iA[i]];
        }

        ctr = 0;
        double dif = 1000.;
        vector<double> XA_old = XA;
        while ((ctr < 500) && (dif > 1e-9)) {
            ctr += 1;
            XA = XA_find(XA, ncA, delta_ij, den, x_assoc);
            dif = 0.;
            for (int i = 0; i < ncA*2; i++) {
                dif += abs(XA[i] - XA_old[i]);
            }
            XA_old = XA;
        }

        vector<double> dXA_dd(ncA*ncA*ncomp, 0);
        dXA_dd = dXA_find(ncA, ncomp, iA, delta_ij, den, XA, ddelta_dd, x_assoc, a_sites);

        for (int i = 0; i < ncomp; i++) {
            for (int j = 0; j < ncA; j++) {
                for (int k = 0; k < a_sites; k++) {
                    mu_assoc[i] += x[iA[j]]*den*dXA_dd[i*(ncA*a_sites)+j*(a_sites)+k]*(1/XA[(j+1)*k]-0.5);
                }
            }
        }

        for (int i = 0; i < ncA; i++) {
            for (int l = 0; l < a_sites; l++) {
                mu_assoc[iA[i]] += log(XA[(i+1)*l])-0.5*XA[(i+1)*l];
            }
            mu_assoc[iA[i]] += 0.5*a_sites;
        }
    }

    // Ion term ---------------------------------------------------------------
    vector<double> mu_ion(ncomp, 0);    
    if (!cppargs.z.empty()) {
        vector<double> q(cppargs.z.begin(), cppargs.z.end());
        for (int i = 0; i < ncomp; i++) {
            q[i] = q[i]*E_CHRG;
        }

        summ = 0.;
        for (int i = 0; i < ncomp; i++) {
            summ += cppargs.z[i]*cppargs.z[i]*x[i];
        }
        double kappa = sqrt(den*E_CHRG*E_CHRG/kb/t/(cppargs.dielc*perm_vac)*summ); // the inverse Debye screening length. Equation 4 in Held et al. 2008.

        if (kappa != 0) {
            vector<double> chi(ncomp); 
            vector<double> sigma_k(ncomp);
            double summ1 = 0.;
            double summ2 = 0.;
            for (int i = 0; i < ncomp; i++) {
                chi[i] = 3/pow(kappa*s[i], 3)*(1.5 + log(1+kappa*s[i]) - 2*(1+kappa*s[i]) +
                    0.5*pow(1+kappa*s[i], 2));
                sigma_k[i] = -2*chi[i]+3/(1+kappa*s[i]);
                summ1 += q[i]*q[i]*x[i]*sigma_k[i];
                summ2 += x[i]*q[i]*q[i];
            }

            for (int i = 0; i < ncomp; i++) {
                mu_ion[i] = -q[i]*q[i]*kappa/24./PI/kb/t/(cppargs.dielc*perm_vac)* 
                    (2*chi[i] + summ1/summ2);
            }
        }
    }

    double Z = pcsaft_Z_cpp(x, m, s, e, t, rho, cppargs);

    vector<double> mu(ncomp, 0);
    vector<double> fugcoef(ncomp, 0);
    for (int i = 0; i < ncomp; i++) {
        mu[i] = mu_hc[i] + mu_disp[i] + mu_polar[i] + mu_assoc[i] + mu_ion[i];
        fugcoef[i] = exp(mu[i] - log(Z)); // the fugacity coefficients
    }

    return fugcoef;
}


double pcsaft_p_cpp(vector<double> x, vector<double> m, vector<double> s, vector<double> e,
    double t, double rho, add_args &cppargs) {
    /**
    Calculate pressure.

    Parameters
    ----------
    x : vector<double>, shape (n,)
        Mole fractions of each component. It has a length of n, where n is
        the number of components in the system.
    m : vector<double>, shape (n,)
        Segment number for each component.
    s : vector<double>, shape (n,)
        Segment diameter for each component. For ions this is the diameter of
        the hydrated ion. Units of Angstrom.
    e : vector<double>, shape (n,)
        Dispersion energy of each component. For ions this is the dispersion
        energy of the hydrated ion. Units of K.
    t : double
        Temperature (K)
    rho : double
        Molar density (mol m^{-3})
    cppargs : add_args
        A struct containing additional arguments that can be passed for 
        use in PC-SAFT:
        
        k_ij : vector<double>, shape (n*n,)
            Binary interaction parameters between components in the mixture. 
            (dimensions: ncomp x ncomp)
        e_assoc : vector<double>, shape (n,)
            Association energy of the associating components. For non associating
            compounds this is set to 0. Units of K.
        vol_a : vector<double>, shape (n,)
            Effective association volume of the associating components. For non 
            associating compounds this is set to 0.
        dipm : vector<double>, shape (n,)
            Dipole moment of the polar components. For components where the dipole 
            term is not used this is set to 0. Units of Debye.
        dip_num : vector<double>, shape (n,)
            The effective number of dipole functional groups on each component 
            molecule. Some implementations use this as an adjustable parameter 
            that is fit to data.
        z : vector<double>, shape (n,)
            Charge number of the ions          
        dielc : double
            Dielectric constant of the medium to be used for electrolyte
            calculations.

    Returns
    -------
    P : double
        Pressure (Pa)
    */
    double den = rho*N_AV/1.0e30;

    double Z = pcsaft_Z_cpp(x, m, s, e, t, rho, cppargs);
    double P = Z*kb*t*den*1.0e30; // Pa
    return P;
}


double pcsaft_den_cpp(vector<double> x, vector<double> m, vector<double> s, vector<double> e,
    double t, double p, int phase, add_args &cppargs) {
    /**
    Solve for the molar density when temperature and pressure are given.

    Parameters
    ----------
    x : vector<double>, shape (n,)
        Mole fractions of each component. It has a length of n, where n is
        the number of components in the system.
    m : vector<double>, shape (n,)
        Segment number for each component.
    s : vector<double>, shape (n,)
        Segment diameter for each component. For ions this is the diameter of
        the hydrated ion. Units of Angstrom.
    e : vector<double>, shape (n,)
        Dispersion energy of each component. For ions this is the dispersion
        energy of the hydrated ion. Units    struct den_params params = {x, m, s, e, t, p, cppargs}; of K.
    t : double
        Temperature (K)
    p : double
        Pressure (Pa)
    phase : int
        The phase for which the calculation is performed. Options: 0 (liquid),
        1 (vapor).
    cppargs : add_args
        A struct containing additional arguments that can be passed for 
        use in PC-SAFT:
        
        k_ij : vector<double>, shape (n*n,)
            Binary interaction parameters between components in the mixture. 
            (dimensions: ncomp x ncomp)
        e_assoc : vector<double>, shape (n,)
            Association energy of the associating components. For non associating
            compounds this is set to 0. Units of K.
        vol_a : vector<double>, shape (n,)
            Effective association volume of the associating components. For non 
            associating compounds this is set to 0.
        dipm : vector<double>, shape (n,)
            Dipole moment of the polar components. For components where the dipole 
            term is not used this is set to 0. Units of Debye.
        dip_num : vector<double>, shape (n,)
            The effective number of dipole functional groups on each component 
            molecule. Some implementations use this as an adjustable parameter 
            that is fit to data.
        z : vector<double>, shape (n,)
            Charge number of the ions          
        dielc : double
            Dielectric constant of the medium to be used for electrolyte
            calculations.

    Returns
    -------
    rho : double
        Molar density (mol m^-3)
    */
    double x_lo, x_hi;
    double rho_guess;
    if (phase == 0) {
        rho_guess = 0.5;
        x_lo = 0.2;
        x_hi = 0.7405;
    }
    else {
        rho_guess = 1.0e-9;
        x_lo = 1.0e-12;
        x_hi = 0.06;
    }

    int ncomp = x.size(); // number of components
    vector<double> d (ncomp);
    double summ = 0., P_fit;
    for (int i = 0; i < ncomp; i++) {
        d[i] = s[i]*(1-0.12*exp(-3*e[i]/t));
        summ += x[i]*m[i]*pow(d[i],3.);
    }

    rho_guess = 6/PI*rho_guess/summ*1.0e30/N_AV;
    x_lo = 6/PI*x_lo/summ*1.0e30/N_AV;
    x_hi = 6/PI*x_hi/summ*1.0e30/N_AV;

    // solving for density using bounded secant method
    double rho, rho1, rho2, dx=1.0e-8, y1, y2=999.0;
    int iter=1, maxiter=300;
    rho1 = rho_guess;
    rho2 = rho1 + dx;
    P_fit = pcsaft_p_cpp(x, m, s, e, t, rho1, cppargs);
    y1 = pow((P_fit-p)/p*100, 2.);
    rho = rho2;
    while (iter < maxiter && y2 > 1.0e-8) {
        P_fit = pcsaft_p_cpp(x, m, s, e, t, rho, cppargs);
        y2 = pow((P_fit-p)/p*100, 2.);
        rho = rho2-y2/(y2-y1)*(rho2-rho1);
        if (rho < x_lo) {
            rho = (x_lo + rho2)/2;
        }
        if (rho > x_hi) {
            rho = (x_hi + rho2)/2;
        }
        y1=y2;
        rho1 = rho2;
        rho2 = rho;
        iter += 1;
    }

    if (y2 > 1.0e-3 && rho == x_hi) {
        x_hi = 0.14;
        x_hi = 6/PI*x_hi/summ*1.0e30/N_AV;
        while (iter < maxiter && y2 > 1.0e-8) {
            P_fit = pcsaft_p_cpp(x, m, s, e, t, rho, cppargs);
            y2 = pow((P_fit-p)/p*100, 2.);
            rho = rho2-y2/(y2-y1)*(rho2-rho1);
            if (rho < x_lo) {
                rho = (x_lo + rho2)/2;
            }
            if (rho > x_hi) {
                rho = (x_hi + rho2)/2;
            }
            y1=y2;
            rho1 = rho2;
            rho2 = rho;
            iter += 1;
        }
    }

    return rho;
}


double bubblePfit_cpp(double p_guess, vector<double> xv_guess, vector<double> x, vector<double> m, vector<double> s, vector<double> e,
    double t, add_args &cppargs) {
    /**Minimize this function to calculate the bubble point pressure.*/
    int ncomp = x.size();
    double error = 0.;

    if (cppargs.z.empty()) { // Check that the mixture does not contain electrolytes. For electrolytes, a different equilibrium criterion should be used. 
        double rho = pcsaft_den_cpp(x, m, s, e, t, p_guess, 0, cppargs);       
        vector<double> fugcoef_l = pcsaft_fugcoef_cpp(x, m, s, e, t, rho, cppargs);
        
        // internal iteration loop for vapor phase composition
        int itr = 0;
        double dif = 10000.;
        double summ;
        vector<double> xv = xv_guess;
        vector<double> xv_old = xv_guess;
        vector<double> fugcoef_v(ncomp, 0);
        while ((dif>1e-9) && (itr<100)) {
            xv_old = xv;
            rho = pcsaft_den_cpp(xv, m, s, e, t, p_guess, 1, cppargs);      
            fugcoef_v = pcsaft_fugcoef_cpp(xv, m, s, e, t, rho, cppargs);
            summ = 0.;
            for (int i = 0; i < ncomp; i++) {
                xv[i] = fugcoef_l[i]*x[i]/fugcoef_v[i];
                summ += xv[i];
            }
            dif = 0.;
            for (int i = 0; i < ncomp; i++) {
                xv[i] = xv[i]/summ;
                dif += abs(xv[i] - xv_old[i]);
            }
            itr += 1;
        }
        for (int i = 0; i < ncomp; i++) {
            error += pow(fugcoef_l[i]*x[i] - fugcoef_v[i]*xv[i], 2.);
        }
    }
    else {
        double rho = pcsaft_den_cpp(x, m, s, e, t, p_guess, 0, cppargs);      
        vector<double> fugcoef_l = pcsaft_fugcoef_cpp(x, m, s, e, t, rho, cppargs);
        
        // internal iteration loop for vapor phase composition
        int itr = 0;
        double dif = 10000., summ;
        vector<double> xv = xv_guess;
        vector<double> xv_old = xv_guess;
        vector<double> fugcoef_v(ncomp, 0);
        while ((dif>1e-9) && (itr<100)) {
            xv_old = xv;
            rho = pcsaft_den_cpp(xv, m, s, e, t, p_guess, 1, cppargs);
            fugcoef_v = pcsaft_fugcoef_cpp(xv, m, s, e, t, rho, cppargs);
            for (int i = 0; i < ncomp; i++) {
                if (cppargs.z[i] == 0) {            
                    xv[i] = fugcoef_l[i]*x[i]/fugcoef_v[i];
                }
            }

            summ = 0.;
            for (int i = 0; i < ncomp; i++) {
                summ += xv[i];
            }

            dif = 0.;
            for (int i = 0; i < ncomp; i++) {
                xv[i] = xv[i]/summ;
                dif += abs(xv[i] - xv_old[i]);
            }
            itr += 1;
        }

        for (int i = 0; i < ncomp; i++) {
            if (cppargs.z[i] == 0) {
                error += pow(fugcoef_l[i]*x[i] - fugcoef_v[i]*xv[i], 2.);
            }
        }
    }

    if (!isfinite(error)) {
        error = 100000000.;
    }
    return error;
}


double PTzfit_cpp(double p_guess, vector<double> x_guess, double beta_guess, double mol, 
    double vol, vector<double> x_total, vector<double> m, vector<double> s, vector<double> e,
    double t, add_args &cppargs) {
    /**Minimize this function to solve for the pressure to compare with PTz data.*/
    int ncomp = x_total.size();
    double error; 
   
    if (cppargs.z.empty()) { // Check that the mixture does not contain electrolytes. For electrolytes, a different equilibrium criterion should be used. 
        // internal iteration loop to solve for compositions
        int itr = 0;
        double dif = 10000.;
        vector<double> xl = x_guess;
        double beta = beta_guess;
        vector<double> fugcoef_l(ncomp), fugcoef_v(ncomp), xv(ncomp);
        for (int i = 0; i < ncomp; i++) {
            xv[i] = (mol*x_total[i] - (1-beta)*mol*xl[i])/beta/mol;
        }
        double rhol, rhov, summ, beta_old;
        while ((dif>1e-9) && (itr<100)) {
            beta_old = beta;
            rhol = pcsaft_den_cpp(xl, m, s, e, t, p_guess, 0, cppargs);     
            fugcoef_l = pcsaft_fugcoef_cpp(xl, m, s, e, t, rhol, cppargs);
            rhov = pcsaft_den_cpp(xv, m, s, e, t, p_guess, 1, cppargs);    
            fugcoef_v = pcsaft_fugcoef_cpp(xv, m, s, e, t, rhov, cppargs);

            summ = 0.;
            for (int i = 0; i < ncomp; i++) {
                xl[i] = fugcoef_v[i]*xv[i]/fugcoef_l[i];
                summ += xl[i];
            }
            for (int i = 0; i < ncomp; i++) {
                xl[i] = xl[i]/summ;
                xv[i] = (mol*x_total[i] - (1-beta)*mol*xl[i])/beta/mol;
            }

            beta = (vol/mol-rhol)/(rhov-rhol);
            dif = abs(beta - beta_old);
            itr += 1;
        }
        
        error = pow((vol - rhov*beta*mol - rhol*(1-beta)*mol),2.);
        for (int i = 0; i < ncomp; i++) {
            error += pow(xl[i]*fugcoef_l[i] - xv[i]*fugcoef_v[i], 2.);
            error += pow((mol*x_total[i] - beta*mol*xv[i] - (1-beta)*mol*xl[i]), 2.);
        }
    }
    else {
        // internal iteration loop to solve for compositions
        int itr = 0;
        double dif = 10000.;
        vector<double> xl = x_guess;
        double beta = beta_guess;
        vector<double> fugcoef_l(ncomp), fugcoef_v(ncomp), xv(ncomp);
        double rhol, rhov, summ=0., beta_old;
        for (int i = 0; i < ncomp; i++) {
            if (cppargs.z[i] != 0) {
                xv[i] = (mol*x_total[i] - (1-beta)*mol*xl[i])/beta/mol;
                summ += 0.;
            }
        }
        for (int i = 0; i < ncomp; i++) {
            xv[i] = xv[i]/summ;
        }
        while ((dif>1e-9) && (itr<100)) {
            beta_old = beta;
            rhol = pcsaft_den_cpp(xl, m, s, e, t, p_guess, 0, cppargs);     
            fugcoef_l = pcsaft_fugcoef_cpp(xl, m, s, e, t, rhol, cppargs);
            rhov = pcsaft_den_cpp(xv, m, s, e, t, p_guess, 1, cppargs);    
            fugcoef_v = pcsaft_fugcoef_cpp(xv, m, s, e, t, rhov, cppargs);

            summ = 0.;
            for (int i = 0; i < ncomp; i++) {
                xl[i] = fugcoef_v[i]*xv[i]/fugcoef_l[i];
                summ += xl[i];
            }
            for (int i = 0; i < ncomp; i++) {
                xl[i] = xl[i]/summ;
                if (cppargs.z[i] != 0) { // here it is assumed that the ionic compounds are nonvolatile
                    xv[i] = (mol*x_total[i] - (1-beta)*mol*xl[i])/beta/mol;
                    summ += 0.;
                }
            }
            for (int i = 0; i < ncomp; i++) {
                xv[i] = xv[i]/summ;
            }

            beta = (vol/mol-rhol)/(rhov-rhol);
            dif = abs(beta - beta_old);
            itr += 1;
        }
        
        error = pow((vol - rhov*beta*mol - rhol*(1-beta)*mol),2.);
        for (int i = 0; i < ncomp; i++) {
            if (cppargs.z[i] != 0) {
                error += pow(xl[i]*fugcoef_l[i] - xv[i]*fugcoef_v[i], 2.);
            }
            error += pow((mol*x_total[i] - beta*mol*xv[i] - (1-beta)*mol*xl[i]), 2.);
        }
    }

    if (!isfinite(error)) {
        error = 100000000.;
    }
    return error;
}