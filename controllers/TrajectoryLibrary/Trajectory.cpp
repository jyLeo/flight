/*
 * Trajectory class representing a trajectory in state space
 *
 * Author: Andrew Barry, <abarry@csail.mit.edu> 2013-2015
 *
 */

#include "Trajectory.hpp"



// Constructor that loads a trajectory from a file
Trajectory::Trajectory() {
    trajectory_number_ = -1;
    dimension_ = 0;
    udimension_ = 0;
    filename_prefix_ = "";
    dt_ = 0;
}

Trajectory::Trajectory(std::string filename_prefix, bool quiet) : Trajectory() {
    LoadTrajectory(filename_prefix, quiet);
}

void Trajectory::LoadTrajectory(std::string filename_prefix, bool quiet)
{
    // open the file
    std::vector<std::vector<std::string>> strs;

    if (!quiet)
    {
        std::cout << "Loading trajectory: " << std::endl << "\t" << filename_prefix << std::endl;
    }

    std::string traj_number_str = filename_prefix.substr(filename_prefix.length() - 5, 5);
    trajectory_number_ = std::stoi(traj_number_str);

    LoadMatrixFromCSV(filename_prefix + "-x.csv", xpoints_, quiet);
    LoadMatrixFromCSV(filename_prefix + "-u.csv", upoints_, quiet);
    LoadMatrixFromCSV(filename_prefix + "-controller.csv", kpoints_, quiet);
    LoadMatrixFromCSV(filename_prefix + "-affine.csv", affine_points_, quiet);

    filename_prefix_ = filename_prefix;

    dimension_ = xpoints_.cols() - 1; // minus 1 because of time index
    udimension_ = upoints_.cols() - 1;

    if (kpoints_.cols() - 1 != dimension_ * udimension_) {
        std::cerr << "Error: expected to have " << dimension_ << "*" << udimension_ << "+1 = " << dimension_ * udimension_ + 1 << " columns in " << filename_prefix << "-controller.csv but found " << kpoints_.cols() << std::endl;
        exit(1);
    }

    if (affine_points_.cols() - 1 != udimension_) {
        std::cerr << "Error: expected to have " << udimension_ << "+1 = " << udimension_ + 1 << " columns in " << filename_prefix << "-affine.csv but found " << affine_points_.cols() << std::endl;
        exit(1);
    }

    if (xpoints_.rows() != upoints_.rows() || xpoints_.rows() != kpoints_.rows() || xpoints_.rows() != affine_points_.rows()) {
        std::cerr << "Error: inconsistent number of rows in CSV files: " << std::endl
            << "\t" << filename_prefix << "-x: " << xpoints_.rows() << std::endl
            << "\t" << filename_prefix << "-u: " << upoints_.rows() << std::endl
            << "\t" << filename_prefix << "-controller: " << kpoints_.rows() << std::endl
            << "\t" << filename_prefix << "-affine: " << affine_points_.rows() << std::endl;

            exit(1);
    }

    if (IsTimeInvariant() == true) {
        // also load a precomputed rollout
        LoadMatrixFromCSV(filename_prefix + "-rollout.csv", xpoints_rollout_, quiet);
    }

}


void Trajectory::LoadMatrixFromCSV( const std::string& filename, Eigen::MatrixXd &matrix, bool quiet) {

    if (!quiet) {
        std::cout << "Loading " << filename << std::endl;
    }

    int number_of_lines = GetNumberOfLines(filename);
    int row_num = 0;

    int i =  0;
    //                                   file, delimiter, first_line_is_header?
    CsvParser *csvparser = CsvParser_new(filename.c_str(), ",", true);
    CsvRow *header;
    CsvRow *row;

    header = CsvParser_getHeader(csvparser);
    if (header == NULL) {
        printf("%s\n", CsvParser_getErrorMessage(csvparser));
        return;
    }

    // note: do not remove the getFields(header) call as it has
    // side effects we need even if we don't use the headers
    char **headerFields = CsvParser_getFields(header);
    for (i = 0 ; i < CsvParser_getNumFields(header) ; i++) {
        //printf("TITLE: %s\n", headerFields[i]);
    }

    matrix.resize(number_of_lines - 1, i); // minus 1 for header, i = number of columns

    while ((row = CsvParser_getRow(csvparser)) ) {
        //printf("NEW LINE:\n");
        char **rowFields = CsvParser_getFields(row);
        for (i = 0 ; i < CsvParser_getNumFields(row) ; i++) {

            matrix(row_num, i) = atof(rowFields[i]);

            //printf("FIELD: %20.20f\n", atof(rowFields[i]));
        }
        CsvParser_destroy_row(row);

        if (row_num == 1) {
            dt_ = matrix(1, 0) - matrix(0, 0);
        } else if (row_num > 1) {
            if (matrix(row_num, 0) - matrix(row_num - 1, 0) - dt_ > 5*std::numeric_limits<double>::epsilon()) {
                std::cerr << "Error: non-constant dt. Expected dt = " << dt_ << " but got matrix[" << row_num << "][0] - matrix[" << row_num - 1 << "][0] = " << matrix(row_num, 0) - matrix(row_num - 1, 0) << " (residual = " << (matrix(row_num, 0) - matrix(row_num - 1, 0) - dt_) << std::endl;

                std::cout << matrix << std::endl;
                exit(1);
            }
        }

        row_num ++;
    }
    CsvParser_destroy(csvparser);


}

int Trajectory::GetNumberOfLines(std::string filename) {
    int number_of_lines = 0;
    std::string line;
    std::ifstream myfile(filename);

    while (getline(myfile, line)) {
        ++number_of_lines;
    }

    return number_of_lines;
}

Eigen::VectorXd Trajectory::GetState(double t) {
    int index = GetIndexFromTime(t);

    Eigen::VectorXd row_vec = xpoints_.row(index);

    return row_vec.tail(xpoints_.cols() - 1); // remove time
}

Eigen::VectorXd Trajectory::GetUCommand(double t) {
    int index = GetIndexFromTime(t);

    Eigen::VectorXd row_vec = upoints_.row(index);

    return row_vec.tail(upoints_.cols() - 1); // remove time
}

Eigen::VectorXd Trajectory::GetRolloutState(double t) {
    int index = GetIndexFromTime(t, true);

    Eigen::VectorXd row_vec = xpoints_rollout_.row(index);

    return row_vec.tail(xpoints_rollout_.cols() - 1); // remove time
}


/**
 * Assuming a constant dt, we can compute the index of a point
 * based on its time.
 *
 * @param t time to find index of
 * @param (optional) use_rollout set to true to use time bounds from rollout instead of xpoints
 * @retval index of nearest point
 */
int Trajectory::GetIndexFromTime(double t, bool use_rollout) {

    // round t to the nearest dt_

   double t0, tf;

   if (!use_rollout) {
        t0 = xpoints_(0,0);
        tf = xpoints_(xpoints_.rows() - 1, 0);
    } else {
        t0 = xpoints_rollout_(0,0);
        tf = xpoints_rollout_(xpoints_rollout_.rows() - 1, 0);
    }

   if (t < t0) {
       return 0;
   } else if (t > tf) {

        if (!use_rollout) {
            return xpoints_.rows() - 1;
        } else {
            return xpoints_rollout_.rows() - 1;
        }
   }

   // otherwise, we are somewhere in the bounds of the trajectory
    int num_dts = t/dt_;
    float remainder = fmod(t, dt_);

    if (remainder > 0.5f*dt_) {
        num_dts++;
    }

    int starting_dts = t0 / dt_;

    return num_dts + starting_dts;

}

double Trajectory::GetTimeAtIndex(int index) {
    return xpoints_(index, 0);
}

double Trajectory::GetMaxTime() {
    double tf = xpoints_(xpoints_.rows() - 1, 0);

    return tf;
}

/**
 * Unpacks the gain matrix for a specific time t.
 *
 * This could be pre-computed if it becomes a performance bottleneck.
 *
 * @param t time along the trajectory
 *
 * @retval gain matrix at that time with dimension: state_dimension x u_dimension
 */
Eigen::MatrixXd Trajectory::GetGainMatrix(double t) {
    int index = GetIndexFromTime(t);

    Eigen::VectorXd k_row = kpoints_.row(index);


    Eigen::MatrixXd k_mat(udimension_, dimension_);

    for (int i = 0; i < udimension_; i++) {

        // +1 because the column index is time
        int start_pos = i * dimension_ + 1;

        k_mat.row(i) = k_row.segment(start_pos, dimension_);
    }

    return k_mat;


}




void Trajectory::Print() {
    std::cout << "------------ Trajectory print -------------" << std::endl;
    std::cout << "Filename: " << filename_prefix_ << std::endl;
    std::cout << "Trajectory number: " << trajectory_number_ << std::endl;
    std::cout << "Dimension: " << dimension_ << std::endl;
    std::cout << "u-dimension: " << udimension_ << std::endl;

    std::cout << " t\t x\t y\t z\t roll\t pitch\t yaw \t xdot\t ydot\t zdot\t rolld\t pitchd\t yawd" << std::endl;

    std::cout << xpoints_ << std::endl;

    std::cout << "------------- u points ----------------" << std::endl;

    std::cout << " t\t u1\t u2\t u3" << std::endl;

    std::cout << upoints_ << std::endl;

    std::cout << "------------- k points ----------------" << std::endl;

    std::cout << kpoints_ << std::endl;

    std::cout << "------------- affine points ----------------" << std::endl;

    std::cout << affine_points_ << std::endl;
}

void Trajectory::GetTransformedPoint(double t, const BotTrans *transform, double *xyz)
{
    // apply the transformation from the global frame: orgin = (0,0,0)
    // to the local frame point

    Eigen::VectorXd state = GetState(t);

    double originalPoint[3];
    originalPoint[0] = state(0);
    originalPoint[1] = state(1);
    originalPoint[2] = state(2);


    bot_trans_apply_vec(transform, originalPoint, xyz);

}

void Trajectory::PlotTransformedTrajectory(bot_lcmgl_t *lcmgl, const BotTrans *transform)
{
    bot_lcmgl_line_width(lcmgl, 2.0f);
    bot_lcmgl_begin(lcmgl, GL_LINE_STRIP);
    for (int i=0; i<int(xpoints_.size()); i++)
    {
        double xyz[3];
        GetTransformedPoint(i, transform, xyz);

        bot_lcmgl_vertex3f(lcmgl, xyz[0], xyz[1], xyz[2]);
    }
    bot_lcmgl_end(lcmgl);
}

#if 0
double Trajectory::DistanceToPoint(double x, double y, double z)
{
    double minDist = -1;

    for (int i=0; i<int(xpoints.size()); i++)
    {
        // find the distance to this point
        double thisDist = sqrt( pow(x-xpoints[i][0],2) + pow(y-xpoints[i][1],2) + pow(z-xpoints[i][2],2) );

        if (minDist < 0 || thisDist < minDist)
        {
            minDist = thisDist;
        }
    }

    return minDist;
}
#endif
