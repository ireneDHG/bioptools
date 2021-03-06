/************************************************************************/
/**

   \file       pdbmakepatch.c
   
   \version    V1.11
   \date       12.03.15
   \brief      Build patches around a surface atom
   
   \copyright  (c) UCL / Dr. Andrew C. R. Martin 2009-2015
   \author     Dr. Andrew C. R. Martin, Anja Baresic
   \par
               Biomolecular Structure & Modelling Unit,
               Department of Biochemistry & Molecular Biology,
               University College,
               Gower Street,
               London.
               WC1E 6BT.
   \par
               andrew@bioinf.org.uk
               andrew.martin@ucl.ac.uk
               
**************************************************************************

   This code is NOT IN THE PUBLIC DOMAIN, but it may be copied
   according to the conditions laid out in the accompanying file
   COPYING.DOC.

   The code may be modified as required, but any modifications must be
   documented so that the person responsible can be identified.

   The code may not be sold commercially or included as part of a 
   commercial product except as described in the file COPYING.DOC.

**************************************************************************

   Description:
   ============

**************************************************************************

   Usage:
   ======
   
   makepatch takes a PDB file where the B-values have been replaced by
   accessibility and the occupancy by VDW radii. Such a file can be
   generated by running as2bval on the .asa file produced by NACCESS.
   The program requires a residue and atom on the surface to be specified
   as the centre of a patch and then grows the patch from that point
   considering all surface atoms within the specified radius that are
   contacting that central atom and in turn contacting atoms already in
   the patch.


**************************************************************************

   Revision History:
   =================

-  V1.0  01.06.09  Original    By: ACRM
-  V1.1  02.06.09  -s command line option added   By: Anja
                   checking for solvent vector in FlagWholeResidues added
                   (This is the same as Anja's working_makepatch.c dated
                   16.12.09)
-  V1.2  26.06.11  Just a cleanup of V1.1 with comments added on the new
                   code  By: ACRM
-  V1.3  09.05.13  Added -c option
-  V1.4  02.10.13  Added -m option
-  V1.5  02.10.13  Modified Anya's code to use the SetFlag() GetFlag()
                   routines that use the 'extras' field rather than the
                   'bval' field.
-  V1.6  04.11.13  Added check that central residue is found in 
                   FlagSolvVecAngles()
-  V1.7  05.11.13  CalcMassCentre() now takes the number of atoms as a
                   parameter and checks that it doesn't go off the atoms
                   array. Was core dumping on very small PDB files
-  V1.8  22.07.14  Renamed deprecated functions with bl prefix.
                   Added doxygen annotation. By: CTP
-  V1.9  19.08.14  Added AsCopy suffix to call to blSelectAtomsPDB() 
                   By: CTP
-  V1.10 06.11.14  Renamed from makepatch
-  V1.11 12.03.15 Changed to allow multi-character chain names

*************************************************************************/
/* Includes
*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "bioplib/pdb.h"
#include "bioplib/macros.h"


/************************************************************************/
/* Defines and macros
*/
#define MAXBUFF            160
#define DEF_RADIUS         18.0
#define DEF_TOLERANCE      0.2
#define DEF_RING_TOLERANCE 1.0
#define DEF_MINACCESS      0.0
#define NCLOSE             10   /* Number of adjacent C-alpha atoms to 
                                   include when claculating centre of 
                                   mass in CalcMassCentre()
                                */

/************************************************************************/
/* Globals
*/

/************************************************************************/
/* Prototypes
*/
int  main(int argc, char **argv);
void MakePatches(PDB *pdb, char *CentreRes, char *CentreAtom,
                 REAL radius, REAL tolerance, PDB *CA, BOOL ringOnly,
                 REAL minAccess);
BOOL FlagSet(PDB *p);
void SetFlag(PDB *p);
void ClearFlag(PDB *p);
void ClearFlags(PDB *pdb);

BOOL ParseCmdLine(int argc, char **argv, char *CentreRes, 
                  char *CentreAtom, char *infile, char *outfile,
                  REAL *radius, REAL *tolerance, BOOL *summary,
                  BOOL *ringOnly, REAL *minAccess);

void FlagSolvVecAngles(PDB *CA, char *Central, int natom);
void DistFromCentral(PDB *pdb, PDB *central);
void MassCentre(PDB *pdb, PDB *central, int *natom, REAL *Masscen_x,
                REAL *Masscen_y, REAL *Masscen_z);
int  CompareFunc(const void* e1, const void* e2);
void CalcMassCentre(PDB **tab, int natoms, 
                    REAL *cen_x, REAL *cen_y, REAL *cen_z);
BOOL CheckVectAngle(PDB * cetral, REAL *Masscen_x, REAL *Masscen_y, 
                    REAL *Masscen_z, PDB *current, REAL *Masscurr_x,
                    REAL *Masscurr_y, REAL *Masscurr_z);

void FlagWholeResidues(PDB *pdb);
void PrintSummary(PDB *p, char *Central);
void CleanUpPDB(PDB *pdb);
void Usage(void);


/************************************************************************/
/*>int main(int argc, char **argv)
   -------------------------------
*//**

-  01.06.09  Original   By: ACRM
-  02.06.09  Added -s command line option   By: Anja
-  22.07.14 Renamed deprecated functions with bl prefix. By: CTP
-  19.08.14 Added AsCopy suffix to call to blSelectAtomsPDB() By: CTP
*/
int main(int argc, char **argv)
{
   FILE *in  = stdin,
        *out = stdout;
   
   char InFile[MAXBUFF],
        OutFile[MAXBUFF],
        CentreRes[MAXBUFF],
        CentreAtom[MAXBUFF]; 
   char *sel[2];
   PDB  *pdb, 
        *Calphas;
   int  natom,
        nCatom;   
   REAL radius = DEF_RADIUS, 
        tolerance = DEF_TOLERANCE,
        minAccess = DEF_MINACCESS;
   BOOL summary,
        ringOnly;

   if(ParseCmdLine(argc, argv, CentreRes, CentreAtom, InFile, OutFile,
                   &radius, &tolerance, &summary, &ringOnly, &minAccess))
   {
      if(blOpenStdFiles(InFile, OutFile, &in, &out))
      {
         if((pdb=blReadPDB(in, &natom))==NULL)
         {
            fprintf(stderr,"pdbmakepatch: (Error) No atoms read from PDB \
file\n");
            return(1);
         }
         
         /* V1.1 By: Anja
            Extract the Calphas as a linked list
            Calculate the solvent vectors - i.e. vectors from the CofG of
            the C-alpha atoms to the Central residue's C-alpha and each 
            other C-alpha.
            Calphas->bval is used as a flag and set to 1 if the angle is
            <120 and to 0 if >=120 

            V1.4 Changed to use 'extras' for the flag
         */
         SELECT(sel[0],"CA  ");
         Calphas = blSelectAtomsPDBAsCopy(pdb, 1, sel, &nCatom);
         FlagSolvVecAngles(Calphas, CentreRes, nCatom);
 
         PADCHARMINTERM(CentreAtom, ' ', 4);
         MakePatches(pdb, CentreRes, CentreAtom, radius, tolerance, 
                     Calphas, ringOnly, minAccess);

         FlagWholeResidues(pdb);
         CleanUpPDB(pdb);
         blWritePDB(out, pdb);
         
         /* V1.1 By: Anja
            Print summary if required
         */
         if (summary)
         {
            PrintSummary(pdb, CentreRes);           
         }         
      }
   }
   else
   {
      Usage();
   }
   
   return(0);
}

/************************************************************************/
/*>void Usage(void)
   ----------------
*//**

-  01.06.09  Original   By: ACRM
-  26.10.11  V1.2
-  09.05.13  V1.3
-  02.10.13  V1.4
-  02.10.13  V1.5
-  04.11.13  V1.6
-  05.11.13  V1.7
-  22.07.14  V1.8 By: CTP
-  06.11.14  V1.10 By: ACRM
-  12.03.15  V1.11
*/
void Usage(void)
{
   fprintf(stderr,"\npdbmakepatch V1.11 Andrew C.R. Martin, Anja \
Baresic, UCL 2009-2015\n");

   fprintf(stderr,"\nUsage: pdbmakepatch [-r radius] [-t tolerance] [-c] \
[-m minaccess]\n");
   fprintf(stderr,"                    resspec atomname [in.pdb \
[out.pdb]]\n");
   fprintf(stderr,"       -r  Specify radius for considering atoms \
[%.2f]\n", (REAL)DEF_RADIUS);
   fprintf(stderr,"       -t  Specify tolerance on atom radii to \
consider them as \n");
   fprintf(stderr,"           touching [%.2f, %.2f if used with -c]\n",
           (REAL)DEF_TOLERANCE, (REAL)DEF_RING_TOLERANCE);
   fprintf(stderr,"       -s  Print a summary of all residues in a \
patch\n");
   fprintf(stderr,"       -c  Ring of contacting residues immediately \
around the central one only\n");
   fprintf(stderr,"       -m  Specify minimum accessibility to consider \
a residue to be on the surface\n");

   fprintf(stderr,"\npdbmakepatch takes a PDB file where the B-values \
have been replaced by\n");
   fprintf(stderr,"accessibility and the occupancy by VDW radii. Such a \
file can be\n");
   fprintf(stderr,"generated by running as2bval on the .asa file \
produced by NACCESS.\n");
   fprintf(stderr,"The program requires a residue and atom on the \
surface to be specified\n");
   fprintf(stderr,"as the centre of a patch and then grows the patch \
from that point \n");
   fprintf(stderr,"considering all surface atoms within the specified \
radius that are \n");
   fprintf(stderr,"contacting that central atom and in turn contacting \
atoms already in\n");
   fprintf(stderr,"the patch.\n\n");
   blPrintResSpecHelp(stderr);
   fprintf(stderr,"\n");
}

/************************************************************************/
/*>void MakePatches(PDB *pdb, char *CentreRes, char *CentreAtom,
                    REAL radius, REAL tolerance, PDB *CA, BOOL ringOnly)
   ---------------------------------------------------------------------
*//**

   Identifies the central atom, clears flags for all atoms then sets
   the central atom flag. Iterates over the PDB file, flagging atoms
   within the required radius of the central atom and within touching
   distance of that atom or other flagged atoms.

-  01.06.09  Original   By: ACRM
-  02.06.09  Added check on solvent vector < 120degrees   By: Anja
-  09.07.13  Added ringOnly   By: ACRM
-  02.10.13  Added minAccess - rather than just using zero
-  22.07.14 Renamed deprecated functions with bl prefix. By: CTP
-  12.03.15 Changed to allow multi-character chain names  By: ACRM
*/
void MakePatches(PDB *pdb, char *CentreRes, char *CentreAtom,
                 REAL radius, REAL tolerance, PDB *CA, BOOL ringOnly,
                 REAL minAccess)
{
   PDB *catom, *p;
   BOOL Found = FALSE;
   BOOL Changed, FoundInCA;
   REAL RadSq = radius * radius;
   
   /* Find the central residue and atom                                */
   catom = blFindResidueSpec(pdb, CentreRes);
   for(p=catom; p!=NULL && (p->insert[0] == catom->insert[0]) &&
      (p->resnum == catom->resnum); NEXT(p))
   {
      if(!strncmp(p->atnam, CentreAtom, 4))
      {
         Found = TRUE;
         break;
      }
   }
   if(!Found)
   {
      fprintf(stderr, "pdbmakepatch: (Error) Couldn't find Residue %s \
Atom %s\n",
              CentreRes, CentreAtom);
      exit(1);
   }
   catom = p;
   
   /* Clear flags and set the flag for the central patch atom           */
   ClearFlags(pdb);
   SetFlag(catom);

   /* Iterate over the PDB file while we make a change                  */
   do
   {
      PDB *p, *q, *r;
      
      Changed = FALSE;
      for(p=pdb; p!=NULL; NEXT(p))
      {
         /* If this is a flagged atom then look for neighbouring atoms  */
         if(FlagSet(p))
         {
            for(q=pdb; q!=NULL; NEXT(q))
            {
               if(p!=q)
               {
                  /* If this atom not yet flagged                       */
                  if(!FlagSet(q))
                  {
                     /* See if it is on the surface and within the 
                        specified radius
                     */
                     if((q->bval > minAccess) && 
                        (DISTSQ(q,catom) < RadSq))
                     {
                        /* If it's in contact distance of the flagged 
                           atom
                        */
                        if(DISTSQ(p,q) < ((p->occ + q->occ + tolerance) *
                                          (p->occ + q->occ + tolerance)))
                        {
                           /* If we are doing a single ring of residues 
                              around
                           */
                           if(ringOnly)
                           {
                              /* Test we are in same residue            */
                              if(!(((p->resnum == q->resnum) &&   
                                    (p->insert[0] == q->insert[0]) &&
                                    CHAINMATCH(p->chain, q->chain)) ||
                                   /* or other residue is the central 
                                      one 
                                   */
                                   ((p->resnum == catom->resnum) && 
                                    (p->insert[0] == catom->insert[0]) &&
                                    CHAINMATCH(p->chain, catom->chain))))
                              {
                                 continue;
                              }
                           }

                           /* V1.1+  By: Anja
                              Check solvvec vector angle is <120 degrees  
                           */
                           FoundInCA = FALSE;
                           
                           for (r=CA; r!=NULL && (!FoundInCA); NEXT(r))
                           {
                              if ( (r->resnum == q->resnum) &&
                                   (!strcmp(r->chain, q->chain)) && 
                                   (!strcmp(r->insert, q->insert)) )
                              {
                                 FoundInCA = TRUE;
                                 
                                 if(FlagSet(r))
                                 {
                                    
                                    /* Set the flag for this atom and 
                                       update the iteration flag to say 
                                       we've done something            
                                    */
                                    SetFlag(q);
                                    Changed = TRUE;
                                 }
#ifdef DEBUG
                                 else
                                 {
                                    fprintf(stderr, "pdbmakepatch: \
(Debug) Residue %s.%d%s failed on angle test\n", 
                                            q->chain, q->resnum, 
                                            q->insert);
                                 }
#endif
                              }
                           }                        
                           /* V1.1-END                                  */
                        }
                     }
                  }
               }
            }
         }
      }
   } while(Changed);
}

/************************************************************************/
/*>void CleanUpPDB(PDB *pdb)
   -------------------------
*//**

   Restores the occupancy and bvalue columns to something sensible

-  01.06.09  Original   By: ACRM
*/
void CleanUpPDB(PDB *pdb)
{
   PDB *p;
   
   for(p=pdb; p!=NULL; NEXT(p))
   {
      p->occ = 1.0;
      if(FlagSet(p))
         p->bval = 1.0;
      else
         p->bval = 0.0;
   }
   ClearFlags(pdb);
}


/************************************************************************/
/*>void FlagWholeResidues(PDB *pdb)
   --------------------------------
*//**

   Extends flagged atoms to include the whole amino acid

-  01.06.09  Original   By: ACRM
-  02.06.09  Added summary output if -s command line option is used  
             By: Anja 
-  22.07.14 Renamed deprecated functions with bl prefix. By: CTP
*/
void FlagWholeResidues(PDB *pdb)
{
   PDB   *res,
         *p,
         *NextRes;
   BOOL  FlagTheResidue;
   
   for(res=pdb; res!=NULL; res=NextRes)
   {
      NextRes = blFindNextResidue(res);
      FlagTheResidue = FALSE;
      for(p=res; p!=NextRes; NEXT(p))
      {
         if(FlagSet(p))
         {
            FlagTheResidue = TRUE;
            break;
         }
      }
      if(FlagTheResidue)
      {
         for(p=res; p!=NextRes; NEXT(p))
         {
            SetFlag(p);
         }        
      }
   }
}


/************************************************************************/
/*>BOOL FlagSet(PDB *p)
   --------------------
*//**

   Tests whether the flag is set

-  01.06.09  Original   By: ACRM
*/
BOOL FlagSet(PDB *p)
{
   if(p->extras != NULL)
      return(TRUE);
   return(FALSE);
}


/************************************************************************/
/*>void PrintSummary(PDB *p, char *Central)
   ----------------------------------------
*//**

   Prints the summary of which residues are in the patch

-  02.06.09  Original   By: Anja
-  22.07.14 Renamed deprecated functions with bl prefix. By: CTP
*/
void PrintSummary(PDB *pdb, char *Central)
{
   PDB *res, 
       *NextRes;   
   
   /* printing patch identifier                                         */
   fprintf (stdout, "<patch %s> ", Central);

   /* printing all residues in that patch (central will be on the list) */
   for (res=pdb; res!=NULL; res=NextRes)
   {
      NextRes = blFindNextResidue(res);

      if (res->bval == 1)
      {
         fprintf(stdout, "%s:%d%s ",
              res->chain, res->resnum, res->insert);
      }      
   }
   fprintf (stdout, "\n");
}


/************************************************************************/
/*>void SetFlag(PDB *p)
   --------------------
*//**

   Sets the flag

-  01.06.09  Original   By: ACRM
*/
void SetFlag(PDB *p)
{
   p->extras = (void *)1;
}


/************************************************************************/
/*>void ClearFlag(PDB *p)
   ----------------------
*//**

   Sets the flag

-  02.10.13  Original   By: ACRM
*/
void ClearFlag(PDB *p)
{
   p->extras = (void *)0;
}


/************************************************************************/
/*>void ClearFlags(PDB *pdb)
   -------------------------
*//**

   Clears all flags

-  01.06.09  Original   By: ACRM
*/
void ClearFlags(PDB *pdb)
{
   PDB *p;
   for(p=pdb; p!=NULL; NEXT(p))
   {
      p->extras = (void *)NULL;
   }
}


/************************************************************************/
/*>BOOL ParseCmdLine(int argc, char **argv, char *CentreRes, 
                     char *CentreAtom, char *infile, char *outfile,
                     REAL *radius, REAL *tolerance, BOOL *summary,
                     BOOL *ringOnly, REAL *minAcess)
   ----------------------------------------------------------------
*//**

   \param[in]      argc         Argument count
   \param[in]      **argv       Argument array
   \param[out]     *CentreRes   
   \param[out]     *CentreAtom  
   \param[out]     *infile      Input file (or blank string)
   \param[out]     *outfile     Output file (or blank string)
   \param[out]     *radius      Radius to include atoms
   \param[out]     *tolerance   Tolerance on contact distance for atoms
   \param[out]     *summary     Is summary output needed?
                                (default: FALSE)
   \param[out]     *ringOnly    Only do residues in contact with central
   \param[out]     *minAccess   minimum accessibility to be on the surface
   \return                      Success?

   Parse the command line

-  01.06.09  Original   By: ACRM   
-  02.06.09  Added -s command line option  By: Anja
-  09.05.13  Added -c command line option  By: ACRM
-  02.10.13  Added -m command line option
*/
BOOL ParseCmdLine(int argc, char **argv, char *CentreRes, 
                  char *CentreAtom, char *infile, char *outfile,
                  REAL *radius, REAL *tolerance, BOOL *summary,
                  BOOL *ringOnly, REAL *minAccess)
{
   BOOL UserTol = FALSE;
   
   argc--;
   argv++;

   infile[0] = outfile[0] = '\0';
   *radius = DEF_RADIUS;
   *tolerance = DEF_TOLERANCE;
   *summary = FALSE;
   *ringOnly = FALSE;
   *minAccess = DEF_MINACCESS;
   
   
   if(!argc)
   {
      return(FALSE);
   }
   
   while(argc)
   {
      if(argv[0][0] == '-')
      {
         if (argv [0][2]!='\0')
         {
           return(FALSE);
         }
         else
         {            
            switch(argv[0][1])
            {
            case 'h':
               return(FALSE);
               break;
            case 'r':
               argv++;
               argc--;
               if(!sscanf(argv[0], "%lf", radius))
                  return(FALSE);
               break;
            case 't':
               argv++;
               argc--;
               if(!sscanf(argv[0], "%lf", tolerance))
                  return(FALSE);
               UserTol = TRUE;
               break;
            case 'm':
               argv++;
               argc--;
               if(!sscanf(argv[0], "%lf", minAccess))
                  return(FALSE);
               break;
            case 's':
               *summary = TRUE;
               break;
            case 'c':
               *ringOnly = TRUE;
               break;
            default:
               return(FALSE);
               break;
            }
         }         
      }
      else
      {
         /* If doing ringOnly and the user hasn't specified the tolerance
            override the default of 0.2 to become 1.0
         */
         if(*ringOnly && !UserTol)
         {
            *tolerance = DEF_RING_TOLERANCE;
         }

         /* Check that there are 2, 3 or 4 arguments left               */
         if(argc < 2 || argc > 4)
            return(FALSE);
         
         /* Copy the first to CentreRes and second one to CentreAtom    */
         strcpy(CentreRes, argv[0]);
         argc--;
         argv++;
         strcpy(CentreAtom, argv[0]);
         argc--;
         argv++;
         
         /* Copy the first to infile                                    */
         if(argc)
         {
            strcpy(infile, argv[0]);
            argc--;
            argv++;
         }
         
         /* If there's another, copy it to outfile                      */
         if(argc)
         {
            strcpy(outfile, argv[0]);
            argc--;
            argv++;
         }
         
         return(TRUE);
      }
      
      argc--;
      argv++;
   }
   return(TRUE);
}


/************************************************************************/
/*>void FlagSolvVecAngles(PDB *CA, char *Central, int natom)
   ---------------------------------------------------------
*//**
 
   \param[in]      *CA          C-alphas-only in linked list
   \param[in]      *Central     Centre of the patch in [c]resnum[i]
   \param[in]      natom        Number of atoms read in *pdb   

   Calculates mass centre vector for central residue. Then calculates 
   mass centre vector for every residue in Calphas, checks its angle 
   with mass centre vector of central and flags CA->bval with 1 if angle 
   is <120 degrees, else CA->bval=0.

-  02.06.09  Original   By: Anja   
-  26.10.11  Changed double to REAL  By: ACRM
-  02.10.13  Changed to use 'extras' for the flag rather than bval
-  04.11.13  Added check that Central residue is found
-  22.07.14 Renamed deprecated functions with bl prefix. By: CTP
*/
void FlagSolvVecAngles(PDB *CA, char *Central, int natom)
{
   PDB  *current,
        *patchCentre;   
   BOOL AngleOK;
   REAL Masscen_x = 0.0, 
        Masscen_y = 0.0, 
        Masscen_z = 0.0;
   REAL Masscurr_x = 0.0, 
        Masscurr_y = 0.0, 
        Masscurr_z = 0.0;

   /* for Central                                                       */
   if((patchCentre = blFindResidueSpec(CA, Central))==NULL)
   {
      fprintf(stderr, "pdbmakepatch: (Error) Couldn't find Residue %s\n", 
              Central);
      exit(1);
   }

   DistFromCentral(CA, patchCentre);
   MassCentre(CA, patchCentre, &natom, 
              &Masscen_x, &Masscen_y, &Masscen_z);

   /* flagging for angles in CA                                         */
   for (current=CA; current!=NULL; NEXT(current))
   {
      DistFromCentral(CA, current);
      MassCentre(CA, current, &natom, 
                 &Masscurr_x, &Masscurr_y, &Masscurr_z);

      AngleOK = CheckVectAngle(patchCentre, &Masscen_x, &Masscen_y,
                               &Masscen_z, current, &Masscurr_x, 
                               &Masscurr_y, &Masscurr_z);          
      
      if (AngleOK)
      {
         SetFlag(current);
      }
      else
      {
         ClearFlag(current);

#ifdef DEBUG
         fprintf(stderr,"pdbmakepatch: (Debug) %s%d%s was eliminated by \
solvvec FlagSolvVecAngles\n", 
                 current->chain, current->resnum, current->insert);
#endif
      }      
   }   
}
         

/************************************************************************/
/*>void DistFromCentral(PDB *pdb, PDB *central)
   --------------------------------------------
*//**

   \param[in]      *pdb     
   \param[in]      *central    Pointer to the central residue

   Adds distance from the central residue to the occ parameter 
   of residues in the same chain as central, all other residues 
   get occ set to 999.99.

-  08.12.08  Original  By: Anja         
-  26.10.11  Changed double to REAL  By: ACRM
-  12.03.15 Changed to allow multi-character chain names
*/
void DistFromCentral(PDB *pdb, PDB *central)
{
   PDB *current;
   REAL dist_sq;    
  
   for (current=pdb; current!=NULL; NEXT(current))
   {
      /* centre of mass is based on atoms within the same 
         chain as central
      */
      if (CHAINMATCH(current->chain, central->chain))
      {
         dist_sq = DISTSQ(central, current);
         current->occ = sqrt(dist_sq);
      }
      else
      {
         current->occ = 999.99;	
      }			
   }
} 


/**********************************************************************/
/*>void MassCentre(PDB *pdb, PDB *central, int *natom, 
                   REAL *Masscen_x,REAL *Masscen_y, 
                   REAL *Masscen_z)
   ----------------------------------------------------
*//**

   \param[in]      *pdb     
   \param[in]      *central       Pointer to the central residue
   \param[in]      *natom         Number of atoms read from PDB file
   \param[in]      *MassCenCoo    Coordinates of centre of mass

   Outputs the coordinates of the beginning and end point of the solvent 
   vector for the central residue. i.e. the centre of mass of the nearest
   NCLOSE atoms

-  08.12.08  Original  By: Anja
-  03.06.09  Returns coordinates rather than printing them
-  26.10.11  Changed double to REAL  By: ACRM
-  05.11.13  CalcMassCentre() now takes the number of atoms
*/
void MassCentre(PDB *pdb, PDB *central, int *natom, REAL *Masscen_x,
                REAL *Masscen_y, REAL *Masscen_z)
{   
   PDB  **tab,  
        *it;
   int  i;
   REAL cen_x = 0, 
        cen_y = 0, 
        cen_z = 0;

   tab = malloc(*natom * sizeof (PDB*));
   
   /* Creates an array of pointers to the nodes in pdb                  */
   for(it = pdb, i=0; i<*natom; i++, it = it->next)
   {             
      tab[i] = it;
   }

   /* sorts these in ascending order of value stored in occ             */
   qsort(tab, *natom, sizeof(PDB*), CompareFunc); 

   /* takes closest 10 residues, returns (x,y,z) for centre of mass     */
   CalcMassCentre(tab, *natom, &cen_x, &cen_y, &cen_z);

   free(tab);

#ifdef DEBUG
   /* prints out coordinates of the central residue C-alpha (solvent
      vector begin) and coordinates of solvent vector end.
      This is the mass vector not the solvent vector, angle is the same 
   */
   fprintf(stdout,"(%.4f,%.4f,%.4f):(%.4f,%.4f,%.4f)\n", 
           central->x, central->y, central->z, cen_x, cen_y, cen_z);
#endif

   /* returning centre of mass coordinates                              */
   *Masscen_x = cen_x;
   *Masscen_y = cen_y;
   *Masscen_z = cen_z;

} 


/************************************************************************/
/*>int CalcMassCentre(PDB **tab, int natoms, 
                      REAL *cen_x, REAL *cen_y, REAL *cen_z)
   -----------------------------------------------------------
*//**

   \param[in]      **tab    
           int    natoms    Number of atoms in tab[]
   \param[out]     *cen_x
   \param[out]     coordinates of centre of mass
   \param[out]     Calculates centre of mass for NCLOSE (10) closest 
                   C-alpha atoms 

-  08.12.08  Original  By: Anja
-  26.10.11  Changed double to REAL  
             Uses NCLOSE instead of hard-coded 10   By: ACRM
-  05.11.13  Added natoms parameter and check on this
*/
void CalcMassCentre(PDB **tab, int natoms, 
                    REAL *cen_x,REAL *cen_y, REAL *cen_z)
{
   int  added_count = 0,
        array_count = 0;
   REAL x_sum = 0, 
        y_sum = 0, 
        z_sum = 0;
 
   while ((added_count < NCLOSE) && (array_count < natoms))
   {
      /* if distance from central != 0 -> excluding coo. of central     */
      if (! ( ((tab[array_count]->occ) > -0.01) && 
              ((tab[array_count]->occ) < 0.01) ) )
      {      
         x_sum += tab[array_count] -> x;
         y_sum += tab[array_count] -> y;
         z_sum += tab[array_count] -> z;
         added_count++;   
      }      
      array_count++;      
   }
   
   /* coordinates of the centre of mass                                 */
   *cen_x = x_sum/NCLOSE;
   *cen_y = y_sum/NCLOSE;
   *cen_z = z_sum/NCLOSE;
}


/**********************************************************************/
/*>int CompareFunc(const void* e1, const void* e2)
   -----------------------------------------------
*//**

   \param[in]      elements to compare
   \param[out]     0 or 1, depending whether the left element1 goes 
                   before, is equal, or goes after element2, respectively

   Defines comparison metric for qsort. Obtained from
   http://www.codeguru.com/forum/archive/index.php/t-315091.html

-  08.12.08    By: Anja
*/
int CompareFunc(const void* e1, const void* e2) 
{
   const PDB* elem1 = *(PDB**)e1;
   const PDB* elem2 = *(PDB**)e2;

   if ( (elem1->occ) < (elem2->occ) )
   {
      return (-1);
   }
   else if ( (elem1->occ) == (elem2->occ) )
   {
      return (0);
   }
   else
   {
      return (1);           
   }
}


/**********************************************************************/
/*>BOOL CheckVectAngle(PDB *central, REAL *Masscen_x, REAL *Masscen_y, 
                    REAL *Masscen_z, PDB *current, REAL *Masscurr_x,
                    REAL *Masscurr_y, REAL *Masscurr_z) 
   -------------------------------------------------------------------
*//**

   \param[in]  *central    Central residue
   \param[in]  *Masscen_x  Coord of mass centre vector of central residue
   \param[in]  *Masscen_y  Coord of mass centre vector of central residue
   \param[in]  *Masscen_z  Coord of mass centre vector of central residue
   \param[in]  *current    Current residue
   \param[in]  *Masscurr_x Coord of mass centre vector of current residue
   \param[in]  *Masscurr_y Coord of mass centre vector of current residue
   \param[in]  *Masscurr_z Coord of mass centre vector of current residue
   \return                 TRUE if angle between the vectors is <120 
                           degrees, else returns false

   Calculates the vector angle. If it is < 120 returns TRUE, otherwise
   FALSE

-  03.06.09  Original  By: Anja
-  26.10.11  Changed double to REAL  By: ACRM
*/
BOOL CheckVectAngle(PDB *central, REAL *Masscen_x, REAL *Masscen_y, 
                    REAL *Masscen_z, PDB *current, REAL *Masscurr_x,
                    REAL *Masscurr_y, REAL *Masscurr_z) 
{
   REAL eq_x_central, eq_y_central, eq_z_central,
        eq_x_current, eq_y_current, eq_z_current,
        dotprod, 
        lenCentral,
        lenCurrent,
        cosVal;   
   /* Calculate vector equations from begin and end point coordinates   */
   /* central = eq_x_central*i + eq_y_central*j + eq_z_central*k        */
   eq_x_central = *Masscen_x - central->x;
   eq_y_central = *Masscen_y - central->y;
   eq_z_central = *Masscen_z - central->z;

   /* current = eq_x_current*i + eq_y_current*j + eq_z_current*k        */
   eq_x_current = *Masscurr_x - current->x;
   eq_y_current = *Masscurr_y - current->y;
   eq_z_current = *Masscurr_z - current->z;

   /* calculate angle between vectors                                   */
   dotprod = (eq_x_central*eq_x_current) + 
             (eq_y_central*eq_y_current) +
             (eq_z_central*eq_z_current);
   
   lenCentral = sqrt( (eq_x_central*eq_x_central) + 
                      (eq_y_central*eq_y_central) +
                      (eq_z_central*eq_z_central) );

   lenCurrent = sqrt( (eq_x_current*eq_x_current) + 
                      (eq_y_current*eq_y_current) +
                      (eq_z_current*eq_z_current) );
   
   cosVal = dotprod / (lenCentral * lenCurrent);
   
   if (cosVal > -0.5)
   {
      return(TRUE);
   }
   else
   {
      return(FALSE);
   }
}
