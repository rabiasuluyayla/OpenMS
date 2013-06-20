// -*- mode: C++; tab-width: 2; -*-
// vi: set ts=2:
//
// --------------------------------------------------------------------------
//                   OpenMS Mass Spectrometry Framework
// --------------------------------------------------------------------------
//  Copyright (C) 2003-2012 -- Oliver Kohlbacher, Knut Reinert
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// --------------------------------------------------------------------------
// $Maintainer: Erhan Kenar $
// $Authors: $
// --------------------------------------------------------------------------

#ifndef OPENMS_TRANSFORMATIONS_RAW2PEAK_PeakPickerRapid_H
#define OPENMS_TRANSFORMATIONS_RAW2PEAK_PeakPickerRapid_H

#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/DATASTRUCTURES/DefaultParamHandler.h>
#include <OpenMS/CONCEPT/ProgressLogger.h>

#include <boost/dynamic_bitset.hpp>

#include <map>
#include <algorithm>
#include <limits>

#define DEBUG_PEAK_PICKING
#undef DEBUG_PEAK_PICKING
//#undef DEBUG_DECONV
namespace OpenMS
{
/**
   @brief This class implements a fast peak-picking algorithm best suited for high resolution MS data (FT-ICR-MS, Orbitrap). In high resolution data, the signals of ions with similar mass-to-charge ratios (m/z) exhibit little or no overlapping and therefore allow for a clear separation. Furthermore, ion signals tend to show well-defined peak shapes with narrow peak width.

   This peak-picking algorithm detects ion signals in raw data and reconstructs the corresponding peak shape by cubic spline interpolation. Signal detection depends on the signal-to-noise ratio which is adjustable by the user (see parameter signal_to_noise). A picked peak's m/z and intensity value is given by the maximum of the underlying peak spline.

   So far, this peak picker was mainly tested on high resolution data. With appropriate preprocessing steps (e.g. noise reduction and baseline subtraction), it might be also applied to low resolution data.

   @htmlinclude OpenMS_PeakPickerRapid.parameters

   @note The peaks must be sorted according to ascending m/z!

   @ingroup PeakPicking
  */


class OPENMS_DLLAPI CmpPeakByIntensity
{
public:
    template <typename PeakType>
    bool operator()(PeakType x, PeakType y) const
    {
        return x.getIntensity() > y.getIntensity();
    }
};

class OPENMS_DLLAPI PeakPickerRapid
        : public DefaultParamHandler,
        public ProgressLogger
{
public:
    /// Constructor
    PeakPickerRapid();

    /// Destructor
    virtual ~PeakPickerRapid();

    /**
    @brief Applies the peak-picking algorithm to a single spectrum (MSSpectrum). The resulting picked peaks are written to the output spectrum.
    */

    template <typename PeakType>
    bool computeTPG(const PeakType& p1, const PeakType& p2, const PeakType& p3, DoubleReal& mu, DoubleReal& sigma, DoubleReal& area) const
    {
        DoubleReal x1(p1.getMZ()), y1(p1.getIntensity());
        DoubleReal x2(p2.getMZ()), y2(p2.getIntensity());
        DoubleReal x3(p3.getMZ()), y3(p3.getIntensity());

        DoubleReal denom(std::log(std::pow(y1, x3 - x2)*std::pow(y2, x1 - x3)*std::pow(y3, x2 - x1)));

        mu = 0.5 * (std::log(std::pow(y1, x3*x3 - x2*x2)*std::pow(y2, x1*x1 - x3*x3)*std::pow(y3, x2*x2 - x1*x1))/denom);

        sigma = std::sqrt(0.5 * ((x1 - x3)*(x2 - x1)*(x3 - x2))/denom);

        area = std::sqrt(2*M_PI*sigma*sigma)*std::pow(y1*y2*y3, 1.0/3.0) * std::exp(((x1 - mu)*(x1 - mu) + (x2 - mu)*(x2 - mu) + (x3 - mu)*(x3 - mu))/(6*sigma*sigma));
				
        return (area != std::numeric_limits<DoubleReal>::infinity());
    }

    DoubleReal computeScaledGaussian(const DoubleReal& x, const DoubleReal& mu, const DoubleReal& sigma, const DoubleReal& area) const
    {
        return (area/std::sqrt(2*M_PI*sigma*sigma))*std::exp(-((x-mu)*(x-mu))/(2*sigma*sigma));
    }


    template <typename PeakType>
    void pick(const MSSpectrum<PeakType>& input, MSSpectrum<PeakType>& output) const
    {
        // copy meta data of the input spectrum
        output.clear(true);
        output.SpectrumSettings::operator=(input);
        output.MetaInfoInterface::operator=(input);
        output.setRT(input.getRT());
        output.setMSLevel(input.getMSLevel());
        output.setName(input.getName());
        output.setType(SpectrumSettings::PEAKS);

        boost::dynamic_bitset<> occupied(input.size());

        bool intensity_type_area = param_.getValue("intensity_type") == "peakarea" ? true : false;

        // find local maxima in raw data
        for (Size i = 2; i < input.size() - 2; ++i)
        {
            DoubleReal central_peak_mz = input[i].getMZ(), central_peak_int = input[i].getIntensity();

            DoubleReal l1_neighbor_mz = input[i-1].getMZ(), l1_neighbor_int = input[i-1].getIntensity();
            DoubleReal r1_neighbor_mz = input[i+1].getMZ(), r1_neighbor_int = input[i+1].getIntensity();

            DoubleReal l2_neighbor_mz = input[i-2].getMZ(), l2_neighbor_int = input[i-2].getIntensity();
            DoubleReal r2_neighbor_mz = input[i+2].getMZ(), r2_neighbor_int = input[i+2].getIntensity();


            // MZ spacing sanity checks
            DoubleReal l1_to_central = std::fabs(central_peak_mz - l1_neighbor_mz);
            DoubleReal l2_to_l1 = std::fabs(l1_neighbor_mz - l2_neighbor_mz);

            DoubleReal central_to_r1 = std::fabs(r1_neighbor_mz - central_peak_mz);
            DoubleReal r1_to_r2 = std::fabs(r2_neighbor_mz - r1_neighbor_mz);

            DoubleReal min_spacing = (l1_to_central < central_to_r1)? l1_to_central : central_to_r1;


            // look for peak cores meeting MZ and intensity/SNT criteria
            if (central_peak_int > 1.0 && l1_neighbor_int > 1.0 && l2_neighbor_int > 1.0 && r1_neighbor_int > 1.0 && r2_neighbor_int > 1.0
                    && l1_to_central < 1.5*min_spacing
                    && l2_to_l1 < 1.5*min_spacing
                    && (l2_neighbor_int < l1_neighbor_int && l1_neighbor_int < central_peak_int)
                    // && central_peak_int > l1_neighbor_int
                    && central_to_r1 < 1.5*min_spacing
                    && r1_to_r2 < 1.5*min_spacing
                    && (r2_neighbor_int < r1_neighbor_int && r1_neighbor_int < central_peak_int)
                    /* && central_peak_int > r1_neighbor_int */
                    )
            {
                // potential triple
                DoubleReal mu(0.0), sigma(0.0), area(0.0);


                // std::cout << input[i-1].getMZ() << " " << input[i-1].getIntensity() << " " << input[i].getMZ() << " " << input[i].getIntensity()
                //          << input[i+1].getMZ() << " " << input[i+1].getIntensity() << std::endl;

                bool compOK = computeTPG(input[i - 1], input[i], input[i + 1], mu, sigma, area);

               //  std::cout << mu << " " << sigma << " " << area << std::endl;


                //                std::map<DoubleReal, DoubleReal> peak_raw_data;

                //                peak_raw_data[central_peak_mz] = central_peak_int;
                //                peak_raw_data[left_neighbor_mz] = left_neighbor_int;
                //                peak_raw_data[right_neighbor_mz] = right_neighbor_int;


                Size k(2);

                // Visualize peak shapes for debugging purposes


                //                                for (DoubleReal mz_it = mu - 3*sigma; mz_it < mu + 3*sigma; mz_it += 0.0001)
                //                                {
                //                                    PeakType peak;
                //                                    peak.setMZ(mz_it);
                //                                    peak.setIntensity(computeScaledGaussian(mz_it, mu, sigma, area));
                //                                    output.push_back(peak);
                //                                }


                // save picked pick into output spectrum
                if (compOK)
								{
									PeakType peak;
                	peak.setMZ(mu);

                	DoubleReal output_intensity = intensity_type_area ? area : computeScaledGaussian(mu, mu, sigma, area);

                	peak.setIntensity(output_intensity);
                	output.push_back(peak);
								}

                // jump over raw data points that have been considered already
                i = i + k - 1;
            }
        }

        return ;
    }

    /**
    @brief Applies the peak-picking algorithm to a map (MSExperiment). This method picks peaks for each scan in the map consecutively. The resulting picked peaks are written to the output map.
    */
    template <typename PeakType>
    void pickExperiment(const MSExperiment<PeakType>& input, MSExperiment<PeakType>& output) const
    {
        // make sure that output is clear
        output.clear(true);

        // copy experimental settings
        static_cast<ExperimentalSettings&>(output) = input;

        // resize output with respect to input
        output.resize(input.size());

        bool ms1_only = param_.getValue("ms1_only").toBool();
        Size progress = 0;

        startProgress(0,input.size(),"picking peaks");
        for (Size scan_idx = 0; scan_idx != input.size(); ++scan_idx)
        {
            if (ms1_only && (input[scan_idx].getMSLevel() != 1))
            {
                output[scan_idx] = input[scan_idx];
            }
            else
            {
                pick(input[scan_idx], output[scan_idx]);
            }
            setProgress(++progress);
        }
        endProgress();

        return ;
    }

protected:
    // signal-to-noise parameter
    // DoubleReal signal_to_noise_;

    //docu in base class
    void updateMembers_();

}; // end PeakPickerRapid

}// namespace OpenMS

#endif