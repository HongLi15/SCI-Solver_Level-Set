/*
 * File:   meshFIM_kernels.h
 * Author: zhisong
 *
 * Created on October 24, 2012, 3:29 PM
 */

#ifndef MESHFIM2D_KERNELS_H
#define  MESHFIM2D_KERNELS_H

#include <mycutil.h>

#define DIMENSION 2

struct count_op
{

  __host__ __device__ int operator () (const int& x, const int& y) const
  {
    return (int)(x > 0) + (int)(y > 0);
  }
};

__global__ void kernel_fill_sequence(int nn, int* sequence)
{
  int tidx = blockIdx.x * blockDim.x + threadIdx.x;
  for (int i = tidx; i < nn; i += blockDim.x * gridDim.x)
  {
    sequence[i] = i;
  }
}

__global__ void CopyOutBack_levelset(int* narrowband_list, int* vert_offsets, LevelsetValueType* vertT, LevelsetValueType* vertT_out)
{
  int bidx = narrowband_list[blockIdx.x];
  int tidx = threadIdx.x;

  int start = vert_offsets[bidx];
  int end = vert_offsets[bidx + 1];
  if (tidx < end - start)
  {
    vertT[tidx + start] = vertT_out[tidx + start];
  }
}

__global__ void kernel_updateT_single_stage(LevelsetValueType timestep, int* narrowband_list,
  int largest_ele_part, int largest_vert_part, int full_ele_num, int* ele, int* ele_offsets, LevelsetValueType* cadv_local,
    int nn, int* vert_offsets, LevelsetValueType* vert, LevelsetValueType* vertT, LevelsetValueType* ele_local_coords,
    int largest_num_inside_mem, int* mem_locations, int* mem_location_offsets, LevelsetValueType* vertT_out)
{
  int bidx = narrowband_list[blockIdx.x];
  int tidx = threadIdx.x;
  int ele_start = ele_offsets[bidx];
  int ele_end = ele_offsets[bidx + 1];
  int vert_start = vert_offsets[bidx];
  int vert_end = vert_offsets[bidx + 1];

  int nv = vert_end - vert_start;
  int ne = ele_end - ele_start;
  //  LevelsetValueType vertices[3][2];
  LevelsetValueType local_coord0, local_coord1, local_coord2;
  LevelsetValueType sigma[2];
  LevelsetValueType alphatuda[3] = {0.0, 0.0, 0.0};
  LevelsetValueType nablaPhi[2] = {0.0, 0.0};
  LevelsetValueType nablaN[3][2];
  LevelsetValueType volume, Hintegral, oldT;
  LevelsetValueType abs_nabla_phi;


  extern __shared__ char s_array[];
  LevelsetValueType* s_vertT = (LevelsetValueType*)s_array;
  //LevelsetValueType* s_vert = (LevelsetValueType*)s_array;
  short* s_mem = (short*)&s_vertT[largest_vert_part]; //temperarily hold the inside_mem_locations
  LevelsetValueType* s_alphatuda_Hintegral = (LevelsetValueType*)s_array;
  LevelsetValueType* s_alphatuda_volume = (LevelsetValueType*)s_array;
  LevelsetValueType* s_grad_phi = (LevelsetValueType*)s_array;
  LevelsetValueType* s_volume = (LevelsetValueType*)s_array;
  LevelsetValueType* s_curv_up = (LevelsetValueType*)s_array;

  //  short* s_mem = (short*)&s_eleT[largest_ele_part * 4];
  short l_mem[16] = {-1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1};

  int count = 0;
  if (tidx < nv)
  {
    int mem_start = mem_location_offsets[vert_start + tidx];
    int mem_end = mem_location_offsets[vert_start + tidx + 1];
    s_vertT[tidx] = vertT[vert_start + tidx];
    oldT = s_vertT[tidx];
    for (int i = mem_start; i < mem_end; i++)
    {
      int lmem = mem_locations[i];
      if ((lmem % full_ele_num) >= ele_start && (lmem % full_ele_num) < ele_end)
      {
        int local_ele_index = (lmem % full_ele_num) - ele_start;
        int ele_off = lmem / full_ele_num;
        s_mem[tidx * largest_num_inside_mem + count] = (short)(local_ele_index * 3 + ele_off);
        count++;
      }
    }
  }

  __syncthreads();

  if (tidx < nv)
  {
#pragma unroll
    for (int i = 0; i < 16; i++)
    {
      if (i < count)
        l_mem[i] = s_mem[tidx * largest_num_inside_mem + i];
    }
  }

  LevelsetValueType eleT[3];
  //  bool isboundary = false;
  if (tidx < ne)
  {
    for (int i = 0; i < 3; i++)
    {
      int global_vidx = ele[i * full_ele_num + ele_start + tidx];
      if (global_vidx >= vert_start && global_vidx < vert_end)
      {
        short local_vidx = (short)(global_vidx - vert_start);
        eleT[i] = s_vertT[local_vidx];
      }
      else
      {
        eleT[i] = vertT[global_vidx];
      }
    }
  }

  __syncthreads();

  if (tidx < ne)
  {
    local_coord0 = ele_local_coords[0 * full_ele_num + ele_start + tidx];
    local_coord1 = ele_local_coords[1 * full_ele_num + ele_start + tidx];
    local_coord2 = ele_local_coords[2 * full_ele_num + ele_start + tidx];
    sigma[0] = cadv_local[ele_start + tidx];
    sigma[1] = cadv_local[full_ele_num + ele_start + tidx];

    LevelsetValueType cross[3];
    LevelsetValueType v01[3] = {local_coord0, 0.0f, 0.0f};
    LevelsetValueType v02[3] = {local_coord1, local_coord2, 0.0f};

    CROSS_PRODUCT(v01, v02, cross);

    volume = LENGTH(cross) / 2.0;

    //compute inverse of 3 by 3 matrix
    LevelsetValueType a11 = 0.0, a12 = 0.0, a13 = 1.0;
    LevelsetValueType a21 = local_coord0, a22 = 0.0, a23 = 1.0;
    LevelsetValueType a31 = local_coord1, a32 = local_coord2, a33 = 1.0;

    LevelsetValueType det = a11 * a22 * a33 + a21 * a32 * a13 + a31 * a12 * a23 - a11 * a32 * a23 - a31 * a22 * a13 - a21 * a12 * a33;

    LevelsetValueType b11 = a22 * a33 - a23 * a32;
    LevelsetValueType b12 = a13 * a32 - a12 * a33;
    LevelsetValueType b13 = a12 * a23 - a13 * a22;

    LevelsetValueType b21 = a23 * a31 - a21 * a33;
    LevelsetValueType b22 = a11 * a33 - a13 * a31;
    LevelsetValueType b23 = a13 * a21 - a11 * a23;

    nablaN[0][0] = b11 / det;
    nablaN[0][1] = b21 / det;
    nablaN[1][0] = b12 / det;
    nablaN[1][1] = b22 / det;
    nablaN[2][0] = b13 / det;
    nablaN[2][1] = b23 / det;

    //compuate grad of Phi

#pragma unroll
    for (int i = 0; i < 3; i++)
    {
      nablaPhi[0] += nablaN[i][0] * eleT[i];
      nablaPhi[1] += nablaN[i][1] * eleT[i];
    }
    abs_nabla_phi = LENGTH2(nablaPhi);

    //compute K and Kplus and Kminus
    LevelsetValueType Kplus[3];
    LevelsetValueType Kminus[3];
    LevelsetValueType K[3];
    Hintegral = 0.0;
    LevelsetValueType beta = 0;
#pragma unroll
    for (int i = 0; i < 3; i++)
    {
      K[i] = volume * DOT_PRODUCT2(sigma, nablaN[i]); // for H(\nabla u) = sigma DOT \nabla 
      Hintegral += K[i] * eleT[i];
      Kplus[i] = fmax(K[i], 0.0);
      Kminus[i] = fmin(K[i], 0.0);
      beta += Kminus[i];
    }

    beta = 1.0 / beta;

    if (fabs(Hintegral) > 1e-8)
    {
      LevelsetValueType delta[3];
#pragma unroll
      for (int i = 0; i < 3; i++)
      {
        delta[i] = Kplus[i] * beta * (Kminus[0] * (eleT[i] - eleT[0]) + Kminus[1] * (eleT[i] - eleT[1]) + Kminus[2] * (eleT[i] - eleT[2]));
      }

      LevelsetValueType alpha[3];
#pragma unroll
      for (int i = 0; i < 3; i++)
      {
        alpha[i] = delta[i] / Hintegral;
      }

      LevelsetValueType theta = 0;
#pragma unroll
      for (int i = 0; i < 3; i++)
      {
        theta += fmax(0.0, alpha[i]);
      }
#pragma unroll
      for (int i = 0; i < 3; i++)
      {
        alphatuda[i] = fmax(alpha[i], 0.0) / theta;
      }
    }

  }
  __syncthreads();

  if (tidx < ne)
  {
    for (int i = 0; i < 3; i++)
    {
      s_alphatuda_Hintegral[tidx * 3 + i] = alphatuda[i] * Hintegral;
    }
  }
  __syncthreads();

  LevelsetValueType up = 0.0, down = 0.0;
  if (tidx < nv)
  {
#pragma unroll
    for (int i = 0; i < 16; i++)
    {
      short lmem = l_mem[i];
      if (lmem > -1)
      {
        up += s_alphatuda_Hintegral[lmem];
      }
    }
  }
  __syncthreads();

  if (tidx < ne)
  {

    for (int i = 0; i < 3; i++)
    {
      s_alphatuda_volume[tidx * 3 + i] = alphatuda[i] * volume;
    }
  }
  __syncthreads();

  if (tidx < nv)
  {
#pragma unroll
    for (int i = 0; i < 16; i++)
    {
      short lmem = l_mem[i];
      if (lmem > -1)
      {
        down += s_alphatuda_volume[lmem];
      }
    }
  }
  __syncthreads();

  if (tidx < ne)
  {
    s_volume[tidx] = volume;
  }
  __syncthreads();

  LevelsetValueType sum_nb_volume = 0.0;
  if (tidx < nv)
  {
#pragma unroll
    for (int i = 0; i < 16; i++)
    {
      short lmem = l_mem[i];
      if (lmem > -1)
      {
        sum_nb_volume += s_volume[lmem / 3];
      }
    }
  }
  __syncthreads();

  if (tidx < ne)
  {
    s_grad_phi[tidx * 2 + 0] = nablaPhi[0] * volume;
    s_grad_phi[tidx * 2 + 1] = nablaPhi[1] * volume;
  }
  __syncthreads();

  LevelsetValueType node_nabla_phi_up[2] = {0.0f, 0.0f};
  if (tidx < nv)
  {
#pragma unroll
    for (int i = 0; i < 16; i++)
    {
      short lmem = l_mem[i];
      if (lmem > -1)
      {
        node_nabla_phi_up[0] += s_grad_phi[lmem / 3 * 2 + 0];
        node_nabla_phi_up[1] += s_grad_phi[lmem / 3 * 2 + 1];
      }
    }
  }
  __syncthreads();

  if (tidx < ne)
  {

    for (int i = 0; i < 3; i++)
    {
      s_curv_up[tidx * 3 + i] = volume * (DOT_PRODUCT2(nablaN[i], nablaN[i]) / abs_nabla_phi * eleT[i] +
          DOT_PRODUCT2(nablaN[i], nablaN[(i + 1) % 3]) / abs_nabla_phi * eleT[(i + 1) % 3] +
          DOT_PRODUCT2(nablaN[i], nablaN[(i + 2) % 3]) / abs_nabla_phi * eleT[(i + 2) % 3]);
    }
  }
  __syncthreads();

  LevelsetValueType curv_up = 0.0f;
  if (tidx < nv)
  {
#pragma unroll
    for (int i = 0; i < 16; i++)
    {
      short lmem = l_mem[i];
      if (lmem > -1)
      {
        curv_up += s_curv_up[lmem];
      }
    }
    if (fabs(down) > 1e-8)
    {
      LevelsetValueType eikonal = up / down;
      LevelsetValueType node_eikonal = LENGTH2(node_nabla_phi_up) / sum_nb_volume;
      vertT_out[vert_start + tidx] = oldT - timestep * eikonal;
    }
    else
    {
      vertT_out[vert_start + tidx] = oldT;
    }
  }
}

__global__ void kernel_compute_vert_ipermute(int nn, int* vert_permute, int* vert_ipermute)
{
  int bidx = blockIdx.x;
  int tidx = bidx * blockDim.x + threadIdx.x;
  for (int vidx = tidx; vidx < nn; vidx += blockDim.x * gridDim.x)
  {
    vert_ipermute[vert_permute[vidx]] = vidx;
  }
}

__global__ void kernel_ele_and_vert(int full_num_ele, int ne, int* ele, int* ele_after_permute, int* ele_permute,
    int nn, LevelsetValueType* vert, LevelsetValueType* vert_after_permute, LevelsetValueType* vertT, 
    LevelsetValueType* vertT_after_permute,
    LevelsetValueType* Rinscribe_before_permute, LevelsetValueType* Rinscribe_after_permute,
    int* vert_permute, int* vert_ipermute)
{
  int bidx = blockIdx.x;
  int tidx = bidx * blockDim.x + threadIdx.x;
  for (int vidx = tidx; vidx < nn; vidx += blockDim.x * gridDim.x)
  {
    int old_vidx = vert_permute[vidx];
    vertT_after_permute[vidx] = vertT[old_vidx];
    for (int i = 0; i < 3; i++)
    {
      vert_after_permute[i * nn + vidx] = vert[i * nn + old_vidx];
    }
  }

  for (int eidx = tidx; eidx < full_num_ele; eidx += blockDim.x * gridDim.x)
  {
    int old_eidx = ele_permute[eidx];
    Rinscribe_after_permute[eidx] = Rinscribe_before_permute[old_eidx];
    for (int i = 0; i < 3; i++)
    {
      int old_vidx = ele[i * ne + old_eidx];
      int new_vidx = vert_ipermute[old_vidx];
      ele_after_permute[i * full_num_ele + eidx] = new_vidx;
    }
  }
}

template<int SZ>
__global__ void kernel_compute_timestep(int full_ele_num, int* narrowband, int* ele_offsets,
    LevelsetValueType* Rinscribe, LevelsetValueType* cadv_local, LevelsetValueType* ceik_global,
    LevelsetValueType* ccurv_global,
    LevelsetValueType* timestep_per_block, LevelsetValueType* Rin_per_block)
{
  int tx = threadIdx.x;
  int block_idx = narrowband[blockIdx.x];
  int start = ele_offsets[block_idx];
  int end = ele_offsets[block_idx + 1];
  int ne = end - start;
  __shared__ LevelsetValueType sdata[SZ];
  LevelsetValueType dtmin1 = LARGENUM, dtmin2 = LARGENUM;
  LevelsetValueType Rin;

  if (tx < ne)
  {
    Rin = Rinscribe[start + tx];
    //    printf("bidx=%d, tidx = %d, Rin 1 = %f\n", block_idx, tx, Rin);
    LevelsetValueType Ccurv = fabs(ccurv_global[start + tx]);
    LevelsetValueType Ceik = fabs(ceik_global[start + tx]);
    LevelsetValueType Cadv[2] = {cadv_local[0 * full_ele_num + start + tx], cadv_local[1 * full_ele_num + start + tx]};
    LevelsetValueType CadvLen = LENGTH2(Cadv);
    if (Ccurv > _EPS) dtmin1 = (4.0 * Rin * Rin) / Ccurv / 2.0 / DIMENSION;
    if (Ceik + CadvLen > _EPS) dtmin2 = (Rin * 2.0) / (Ceik + CadvLen) / DIMENSION;
    sdata[tx] = fmin(dtmin1, dtmin2);
  }
  else
    sdata[tx] = LARGENUM;

  __syncthreads();

  for (unsigned int s = SZ / 2; s > 0; s >>= 1)
  {
    if (tx < s)
    {
      sdata[tx] = fmin(sdata[tx], sdata[tx + s]);
    }
    __syncthreads();
  }

  if (tx == 0)
  {
    timestep_per_block[blockIdx.x] = sdata[0];
  }

  __syncthreads();

  if (tx < ne)
  {
    sdata[tx] = Rin;
  }
  else
    sdata[tx] = -1;

  __syncthreads();

  for (unsigned int s = SZ / 2; s > 0; s >>= 1)
  {
    if (tx < s)
    {
      sdata[tx] = fmax(sdata[tx], sdata[tx + s]);
    }
    __syncthreads();
  }

  if (tx == 0)
  {
    Rin_per_block[blockIdx.x] = sdata[0];
  }
}

__global__ void kernel_compute_local_coords(int full_num_ele, int nn, int* ele, int* ele_offsets, LevelsetValueType* vert, LevelsetValueType* ele_local_coords,
    LevelsetValueType* cadv_global, LevelsetValueType* cadv_local)
{
  int tidx = blockIdx.x * blockDim.x + threadIdx.x;
  for (int eidx = tidx; eidx < full_num_ele; eidx += blockDim.x * gridDim.x)
  {
    int ele0 = ele[0 * full_num_ele + eidx];
    int ele1 = ele[1 * full_num_ele + eidx];
    int ele2 = ele[2 * full_num_ele + eidx];

    LevelsetValueType x0 = vert[0 * nn + ele0];
    LevelsetValueType y0 = vert[1 * nn + ele0];
    LevelsetValueType z0 = vert[2 * nn + ele0];

    LevelsetValueType x1 = vert[0 * nn + ele1];
    LevelsetValueType y1 = vert[1 * nn + ele1];
    LevelsetValueType z1 = vert[2 * nn + ele1];

    LevelsetValueType x2 = vert[0 * nn + ele2];
    LevelsetValueType y2 = vert[1 * nn + ele2];
    LevelsetValueType z2 = vert[2 * nn + ele2];

    LevelsetValueType cross[3], local_Y[3];
    LevelsetValueType AB[3] = {x1 - x0, y1 - y0, z1 - z0};
    LevelsetValueType AC[3] = {x2 - x0, y2 - y0, z2 - z0};
    CROSS_PRODUCT(AB, AC, cross);
    CROSS_PRODUCT(cross, AB, local_Y);
    LevelsetValueType len_local_Y = LENGTH(local_Y);
    LevelsetValueType edgelenAB = LENGTH(AB);
    ele_local_coords[0 * full_num_ele + eidx] = edgelenAB;
    ele_local_coords[1 * full_num_ele + eidx] = DOT_PRODUCT(AC, AB) / edgelenAB;
    ele_local_coords[2 * full_num_ele + eidx] = DOT_PRODUCT(AC, local_Y) / len_local_Y;

    LevelsetValueType old_cadv[3] = {
      cadv_global[0 * full_num_ele + eidx],
      cadv_global[1 * full_num_ele + eidx],
      cadv_global[2 * full_num_ele + eidx]};
    LevelsetValueType sigma0 = DOT_PRODUCT(old_cadv, AB) / edgelenAB;
    LevelsetValueType sigma1 = DOT_PRODUCT(old_cadv, local_Y) / len_local_Y;

    cadv_local[0 * full_num_ele + eidx] = sigma0;
    cadv_local[1 * full_num_ele + eidx] = sigma1;
  }
}

__global__ void kernel_fill_ele_label(int ne, int* ele_permute, int* ele_offsets, int* npart, int* ele, int* ele_label)
{
  int bidx = blockIdx.x;
  int tidx = blockIdx.x * blockDim.x + threadIdx.x;

  for (int eidx = tidx; eidx < ne; eidx += blockDim.x * gridDim.x)
  {
    int part0 = npart[ele[0 * ne + eidx]];
    int part1 = npart[ele[1 * ne + eidx]];
    int part2 = npart[ele[2 * ne + eidx]];

    int start = ele_offsets[eidx];
    int end = ele_offsets[eidx + 1];
    int n = end - start;
    for (int j = 0; j < n; j++) ele_permute[start + j] = eidx;
    ele_label[start] = part0;
    int i = 1;
    if (part1 != part0)
    {
      ele_label[start + i] = part1;
      i++;
    }
    if (part2 != part0 && part2 != part1)
    {
      ele_label[start + i] = part2;
      i++;
    }

    if (i != n)
    {
      printf("bidx = %d, tidx = %d, i!=n!!\n", bidx, tidx);
    }
  }
}

__global__ void kernel_compute_ele_npart(int ne, int* npart, int* ele, int* ele_label)
{
  //int bidx = blockIdx.x;
  int tidx = blockIdx.x * blockDim.x + threadIdx.x;

  for (int eidx = tidx; eidx < ne; eidx += blockDim.x * gridDim.x)
  {
    int part0 = npart[ele[0 * ne + eidx]];
    int part1 = npart[ele[1 * ne + eidx]];
    int part2 = npart[ele[2 * ne + eidx]];

    int n = 1;

    if (part1 != part0) n++;
    if (part2 != part0 && part2 != part1) n++;

    ele_label[eidx] = n;
  }
}

__global__ void permuteInitialAdjacencyKernel(int size, int *adjIndexesIn, int *adjacencyIn, 
  int *permutedAdjIndexesIn, int *permutedAdjacencyIn, int *ipermutation, int *fineAggregate, int* neighbor_part)
{
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx < size)
  {
    int oldBegin = adjIndexesIn[ipermutation[idx]];
    int oldEnd = adjIndexesIn[ipermutation[idx] + 1];
    int runSize = oldEnd - oldBegin;
    int newBegin = permutedAdjIndexesIn[idx];
    // Transfer old adjacency into new, while changing node id's with partition id's
    for (int i = 0; i < runSize; i++)
    {
      permutedAdjacencyIn[newBegin + i] = fineAggregate[ adjacencyIn[oldBegin + i] ];
    }
  }
}

__global__ void getInducedGraphNeighborCountsKernel(int size, int *aggregateIdx, 
  int *adjIndexesOut, int *permutedAdjIndexes, int *permutedAdjacencyIn)
{
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx < size)
  {
    int Begin = permutedAdjIndexes[ aggregateIdx[idx] ];
    int End = permutedAdjIndexes[ aggregateIdx[idx + 1] ];

    // Sort each section of the adjacency:
    for (int i = Begin; i < End - 1; i++)
    {
      for (int ii = i + 1; ii < End; ii++)
      {
        if (permutedAdjacencyIn[i] < permutedAdjacencyIn[ii])
        {
          int temp = permutedAdjacencyIn[i];
          permutedAdjacencyIn[i] = permutedAdjacencyIn[ii];
          permutedAdjacencyIn[ii] = temp;
        }
      }
    }

    // Scan through the sorted adjacency to get the condensed adjacency:
    int neighborCount = 1;
    if (permutedAdjacencyIn[Begin] == idx)
      neighborCount = 0;

    for (int i = Begin + 1; i < End; i++)
    {
      if (permutedAdjacencyIn[i] != permutedAdjacencyIn[i - 1] && permutedAdjacencyIn[i] != idx)
      {
        permutedAdjacencyIn[neighborCount + Begin] = permutedAdjacencyIn[i];
        neighborCount++;
      }
    }

    // Store the size
    adjIndexesOut[idx] = neighborCount;
  }
}

__global__ void fillCondensedAdjacencyKernel(int size, int *aggregateIdx, int *adjIndexesOut, 
  int *adjacencyOut, int *permutedAdjIndexesIn, int *permutedAdjacencyIn)
{
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx < size)
  {
    int oldBegin = permutedAdjIndexesIn[ aggregateIdx[idx] ];
    int newBegin = adjIndexesOut[idx];
    int runSize = adjIndexesOut[idx + 1] - newBegin;

    // Copy adjacency over
    for (int i = 0; i < runSize; i++)
    {
      adjacencyOut[newBegin + i] = permutedAdjacencyIn[oldBegin + i];
    }
  }
}

__global__ void mapAdjacencyToBlockKernel(int size, int *adjIndexes, int *adjacency, 
  int *adjacencyBlockLabel, int *blockMappedAdjacency, int *fineAggregate)
{
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx < size)
  {
    int begin = adjIndexes[idx];
    int end = adjIndexes[idx + 1];
    int thisBlock = fineAggregate[idx];

    // Fill block labeled adjacency and block mapped adjacency vectors
    for (int i = begin; i < end; i++)
    {
      int neighbor = fineAggregate[adjacency[i]];

      if (thisBlock == neighbor)
      {
        adjacencyBlockLabel[i] = -1;
        blockMappedAdjacency[i] = -1;
      }
      else
      {
        adjacencyBlockLabel[i] = thisBlock;
        blockMappedAdjacency[i] = neighbor;
      }
    }
  }
}

__global__ void findPartIndicesNegStartKernel(int size, int *array, int *partIndices)
{
  int idx = blockIdx.x * blockDim.x + threadIdx.x + 1;
  if (idx < size)
  {
    int value = array[idx];
    int nextValue = array[idx + 1];
    if (value != nextValue)
      partIndices[value + 1] = idx;
  }
}

__global__ void kernel_compute_vertT_before_permute(int nn, int* vert_permute, 
  LevelsetValueType* vertT_after_permute, LevelsetValueType* vertT_before_permute)
{
  int bidx = blockIdx.x;
  int tidx = bidx * blockDim.x + threadIdx.x;
  for (int vidx = tidx; vidx < nn; vidx += blockDim.x * gridDim.x)
  {
    int old_vidx = vert_permute[vidx];
    vertT_before_permute[old_vidx] = vertT_after_permute[vidx];
  }
}

#endif  /* MESHFIM_KERNELS_H */

