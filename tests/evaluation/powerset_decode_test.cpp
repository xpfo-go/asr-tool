#include "powerset_decode.hpp"

#include <Eigen/Core>

#include <iostream>
#include <stdexcept>

int main() {
    try {
        Eigen::MatrixXi mapping(7, 3);
        mapping << 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,
                   1, 1, 0, 1, 0, 1, 0, 1, 1;
        Eigen::MatrixXd probability(4, 7);
        probability << .04, .30, .05, .01, .29, .01, .30,
                       .60, .10, .10, .05, .05, .05, .05,
                       .01, .01, .01, .01, .01, .94, .01,
                       .01, .01, .01, .01, .32, .32, .32;
        Eigen::MatrixXd scores = probability.array().log();
        const auto labels = minimum_risk_powerset_labels(scores, mapping);
        if (labels.row(0) != mapping.row(4))
            throw std::runtime_error("Marginals must recover overlap hidden by a winning single class");
        if (labels.row(1) != mapping.row(0) || labels.row(2) != mapping.row(5))
            throw std::runtime_error("Confident silence and overlap must be preserved");
        if (labels.row(3).sum() != 2)
            throw std::runtime_error("Marginals must not exceed the model's simultaneous speaker capacity");
        Eigen::MatrixXd shifted = scores.array() - 10000;
        if (minimum_risk_powerset_labels(shifted, mapping) != labels)
            throw std::runtime_error("Log probabilities must remain stable under a common offset");
        Eigen::MatrixXd empty(0, 7);
        if (minimum_risk_powerset_labels(empty, mapping).rows() != 0)
            throw std::runtime_error("Empty input must stay empty");
        Eigen::MatrixXd certain = Eigen::MatrixXd::Constant(1, 7, -INFINITY);
        certain(0, 2) = 0;
        if (minimum_risk_powerset_labels(certain, mapping).row(0) != mapping.row(2))
            throw std::runtime_error("Impossible classes may have negative infinite log probability");
        for (double invalid : {NAN, INFINITY, -INFINITY}) {
            Eigen::MatrixXd bad = Eigen::MatrixXd::Constant(1, 7, invalid);
            bool rejected = false;
            try { (void)minimum_risk_powerset_labels(bad, mapping); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Invalid scores must fail explicitly");
        }
        std::cout << "powerset decode tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
