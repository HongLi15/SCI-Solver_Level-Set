#include <LevelSet.h>

int main(int argc, char *argv[])
{
  LevelSet::LevelSet data;
  bool fromCenter = false;
  //input filename (minus extension)
  std::string filename;
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i],"-v") == 0) {
      data.verbose_ = true;
    } else if (strcmp(argv[i],"-i") == 0) {
      if (i+1 >= argc) break;
      data.filename_ = std::string(argv[i+1]);
      if (data.filename_.substr(data.filename_.size()-5,5) == ".node")
        data.filename_ = data.filename_.substr(0,data.filename_.size() - 5);
      if (data.filename_.substr(data.filename_.size()-4,4) == ".ele")
        data.filename_ = data.filename_.substr(0,data.filename_.size() - 4);
      i++;
    } else if (strcmp(argv[i],"-n") == 0) {
      if (i+1 >= argc) break;
      data.numSteps_ = atoi(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-t") == 0) {
      if (i+1 >= argc) break;
      data.timeStep_ = atof(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-s") == 0) {
      if (i+1 >= argc) break;
      data.insideIterations_ = atoi(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-d") == 0) {
      if (i+1 >= argc) break;
      data.sideLengths_ = atoi(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-p") == 0) {
      if (i+1 >= argc) break;
      data.partitionType_ = atoi(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-m") == 0) {
      if (i+1 >= argc) break;
      data.metisSize_ = atoi(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-b") == 0) {
      if (i+1 >= argc) break;
      data.blockSize_ = atoi(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-w") == 0) {
      if (i+1 >= argc) break;
      data.bandwidth_ = atof(argv[i+1]);
      i++;
    } else if (strcmp(argv[i],"-c") == 0) {
      fromCenter = true;
    } else if (strcmp(argv[i],"-h") == 0) {
      std::cout << "Usage: ./Example1 [OPTIONS]" << std::endl;
      std::cout << "   -h                 Print this help message." << std::endl;
      std::cout << "   -v                 Print verbose runtime information." << std::endl;
      std::cout << "   -i FILENAME        Use this input tet mesh (node/ele)." << std::endl;
      std::cout << "   -n NSTEPS          # of steps to take of TIMESTEP amount." << std::endl;
      std::cout << "   -t TIMESTEP        Duration of a timestep." << std::endl;
      std::cout << "   -s INSIDE_NITER    # of inside iterations." << std::endl;
      std::cout << "   -d NSIDE           # of sides for Square partition type." << std::endl;
      std::cout << "   -p PARTITION_TYPE  1 for Square, otherwise is it METIS." << std::endl;
      std::cout << "   -b NUM_BLOCKS      # of blocks for Square partition type." << std::endl;
      std::cout << "   -m METIS_SIZE      The size for METIS partiation type." << std::endl;
      std::cout << "   -w BANDWIDTH       The Bandwidth for the algorithm." << std::endl;
      exit(0);
    }
  }
  if (fromCenter) {
    //initialize values
    LevelSet::initializeMesh(data);
    std::vector<double> vals;
    for (size_t i = 0; i < LevelSet::mesh_->vertices.size(); i++) {
      point p = LevelSet::mesh_->vertices[i] - point(54.,54.,54.);
      double mag = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
      vals.push_back(mag - 10.);
    }
    std::vector<point> adv;
    for (size_t i = 0; i < LevelSet::mesh_->tets.size(); i++) {
      point p = (LevelSet::mesh_->vertices[LevelSet::mesh_->tets[i][0]] +
          LevelSet::mesh_->vertices[LevelSet::mesh_->tets[i][1]]  +
          LevelSet::mesh_->vertices[LevelSet::mesh_->tets[i][2]])
        / 3. - point(54.,54.,54);
      double mag = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
      adv.push_back(p / mag);
    }
    LevelSet::initializeVertices(data,vals);
    LevelSet::initializeAdvection(data,adv);
  }
  LevelSet::solveLevelSet(data);
  LevelSet::writeVTK();
  return 0;
}

