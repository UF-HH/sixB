// skim_ntuple.exe --input input/PrivateMC_2018/NMSSM_XYH_YToHH_6b_MX_600_MY_400.txt --cfg config/skim_ntuple_2018.cfg  --output prova.root --is-signal

#include <iostream>
#include <string>
#include <iomanip>
#include <any>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include "CfgParser.h"
#include "NanoAODTree.h"

#include "SkimUtils.h"
namespace su = SkimUtils;

#include "OutputTree.h"
#include "jsonLumiFilter.h"

#include "SixB_functions.h"
#include "JetTools.h"

#include "TFile.h"
#include "TROOT.h"

using namespace std;

std::vector<std::string> split_by_delimiter(std::string input, std::string delimiter)
{
    std::vector<std::string> tokens;
    if(input == "")
        return tokens;

    size_t pos = 0;
    while ((pos = input.find(delimiter)) != std::string::npos)
    {
        tokens.push_back(input.substr(0, pos));
        input.erase(0, pos + delimiter.length());
    }
    tokens.push_back(input); // last part splitted

    return tokens;
}

Variation string_to_jer_variation (std::string s)
{
    if (s == "nominal")
        return Variation::NOMINAL;
    if (s == "up")
        return Variation::UP;
    if (s == "down")
        return Variation::DOWN;
    throw std::runtime_error(string("Cannot parse the variation ") + s);
}

int main(int argc, char** argv)
{
    cout << "[INFO] ... starting program" << endl;

    ////////////////////////////////////////////////////////////////////////
    // Declare command line options
    ////////////////////////////////////////////////////////////////////////
    
    po::options_description desc("Skim options");
    desc.add_options()
        ("help", "produce help message")
        // required
        ("cfg"   , po::value<string>()->required(), "skim config")
        ("input" , po::value<string>()->required(), "input file list")
        ("output", po::value<string>()->required(), "output file LFN")
        // optional
        // ("xs"        , po::value<float>(), "cross section [pb]")
        ("maxEvts"   , po::value<int>()->default_value(-1), "max number of events to process")
        ("puWeight"  , po::value<string>()->default_value(""), "PU weight file name")
        ("seed"      , po::value<int>()->default_value(12345), "seed to be used in systematic uncertainties such as JEC, JER, etc")
        // ("kl-rew-list"  , po::value<std::vector<float>>()->multitoken()->default_value(std::vector<float>(0), "-"), "list of klambda values for reweight")
        // ("kl-rew"    , po::value<float>(),  "klambda value for reweighting")
        // ("kl-map"    , po::value<string>()->default_value(""), "klambda input map for reweighting")
        // ("kl-histo"  , po::value<string>()->default_value("hhGenLevelDistr"), "klambda histogram name for reweighting")
        ("jes-shift-syst",  po::value<string>()->default_value("nominal"), "Name of the JES (scale) source uncertainty to be shifted. Usage as <name>:<up/down>. Pass -nominal- to not shift the jets")
        ("jer-shift-syst",  po::value<string>()->default_value("nominal"), "Name of the JER (resolution) source uncertainty to be shifted. Usage as <up/down>. Pass -nominal- to not shift the jets")
        ("bjer-shift-syst", po::value<string>()->default_value("nominal"), "Name of the b regressed JER (resolution) source uncertainty to be shifted. Usage as <up/down>. Pass -nominal- to not shift the jets")
        // pairing variables
        // ("bbbbChoice"    , po::value<string>()->default_value("BothClosestToDiagonal"), "bbbb pairing choice")
        // ("mh1mh2"        , po::value<float>()->default_value(1.05), "Ratio Xo/Yo or 1/slope of the diagonal") 
        // ("option"        , po::value<int>()->default_value(0), "Option: 0=Nominal, 1=Alternative 1, 2=Alternative 2") 
        // flags
        ("is-data",       po::value<bool>()->zero_tokens()->implicit_value(true)->default_value(false), "mark as a data sample (default is false)")
        ("is-signal",     po::value<bool>()->zero_tokens()->implicit_value(true)->default_value(false), "mark as a HH signal sample (default is false)")
        //
        ("save-p4",       po::value<bool>()->zero_tokens()->implicit_value(true)->default_value(false), "save the tlorentzvectors in the output")
    ;

    po::variables_map opts;
    try {
        po::store(parse_command_line(argc, argv, desc, po::command_line_style::unix_style ^ po::command_line_style::allow_short), opts);
        if (opts.count("help")) {
            cout << desc << "\n";
            return 1;
        }
        po::notify(opts);
    }    
    catch (po::error& e) {
        cerr << "** [ERROR] " << e.what() << endl;
        return 1;
    }

    ////////////////////////////////////////////////////////////////////////
    // Read config and other cmd line options for skims
    ////////////////////////////////////////////////////////////////////////

    const bool is_data = opts["is-data"].as<bool>();
    cout << "[INFO] ... is a data sample? " << std::boolalpha << is_data << std::noboolalpha << endl;

    const bool is_signal = (is_data ? false : opts["is-signal"].as<bool>());
    cout << "[INFO] ... is a signal sample? " << std::boolalpha << is_signal << std::noboolalpha << endl;

    CfgParser config;
    if (!config.init(opts["cfg"].as<string>())){
        cerr << "** [ERROR] no config file was provuded" << endl;
        return 1;
    }
    cout << "[INFO] ... using config file " << opts["cfg"].as<string>() << endl;

    ////////////////////////////////////////////////////////////////////////
    // Prepare event loop
    ////////////////////////////////////////////////////////////////////////

    cout << "[INFO] ... opening file list : " << opts["input"].as<string>().c_str() << endl;
    if ( access( opts["input"].as<string>().c_str(), F_OK ) == -1 ){
        cerr << "** [ERROR] The input file list does not exist, aborting" << endl;
        return 1;        
    }

    // Joining all the NANOAD input file in a TChain in order to be used like an unique three
    TChain ch("Events");
    int nfiles = su::appendFromFileList(&ch, opts["input"].as<string>());
    
    if (nfiles == 0){
        cerr << "** [ERROR] The input file list contains no files, aborting" << endl;
        return 1;
    }
    cout << "[INFO] ... file list contains " << nfiles << " files" << endl;

    cout << "[INFO] ... creating tree reader" << endl;

    // The TChain is passed to the NanoAODTree_SetBranchImpl to parse all the branches
    NanoAODTree nat (&ch);

    ////////////////////////////////////////////////////////////////////////
    // Trigger information
    ////////////////////////////////////////////////////////////////////////

    cout << "[INFO] ... loading " << config.readStringListOpt("triggers::makeORof").size() << " triggers" << endl;

    const bool apply_trigger =  config.readBoolOpt("triggers::applyTrigger");
    cout << "[INFO] ... is the OR decision of these triggers applied? " << std::boolalpha << apply_trigger << std::noboolalpha << endl;

    std::vector<std::string> triggerAndNameVector;
    if(apply_trigger) triggerAndNameVector = config.readStringListOpt("triggers::makeORof");
    std::vector<std::string> triggerVector;
    // <triggerName , < objectBit, minNumber> >
    std::map<std::string, std::map< std::pair<int,int>, int > > triggerObjectAndMinNumberMap;

    cout << "[INFO] ... listing the triggers applied" << endl;
    for (auto & trigger : triggerAndNameVector)
    {
        if(trigger == "")
            continue;
        
        std::vector<std::string> triggerTokens = split_by_delimiter(trigger, ":");
        if (triggerTokens.size() != 2)
            throw std::runtime_error("** skim_ntuple : could not parse trigger entry " + trigger + " , aborting");

        triggerVector.push_back(triggerTokens[1]);
        cout << "   - " << triggerTokens[0] << "  ==> " << triggerTokens[1] << endl;

        // if(!config.hasOpt( Form("triggers::%s_ObjectRequirements",triggerTokens[0].data()) ))
        // {
        //     cout<<Form("triggers::%s_ObjectRequirements",triggerTokens[0].data())<<std::endl;
        //     cout<<"Trigger "<< triggerTokens[1] <<" does not have ObjectRequirements are not defined";
        //     continue;
        // }

        // triggerObjectAndMinNumberMap[triggerTokens[1]] = std::map< std::pair<int,int>, int>();   

        // std::vector<std::string> triggerObjectMatchingVector = config.readStringListOpt(Form("triggers::%s_ObjectRequirements",triggerTokens[0].data()));

        // for (auto & triggerObject : triggerObjectMatchingVector)
        // {

        //     std::vector<std::string> triggerObjectTokens;
        //     while ((pos = triggerObject.find(delimiter)) != std::string::npos)
        //     {
        //         triggerObjectTokens.push_back(triggerObject.substr(0, pos));
        //         triggerObject.erase(0, pos + delimiter.length());
        //     }
        //     triggerObjectTokens.push_back(triggerObject); // last part splitted
        //     if (triggerObjectTokens.size() != 3)
        //     {
        //         throw std::runtime_error("** skim_ntuple : could not parse trigger entry " + triggerObject + " , aborting");
        //     }

        //     triggerObjectAndMinNumberMap[triggerTokens[1]][std::pair<int,int>(atoi(triggerObjectTokens[0].data()),atoi(triggerObjectTokens[1].data()))] = atoi(triggerObjectTokens[2].data());
        // }
    }

    nat.triggerReader().setTriggers(triggerVector);

    ////////////////////////////////////////////////////////////////////////
    // Prepare the output
    ////////////////////////////////////////////////////////////////////////
 
    string outputFileName = opts["output"].as<string>();
    cout << "[INFO] ... saving output to file : " << outputFileName << endl;
    TFile outputFile(outputFileName.c_str(), "recreate");
    OutputTree ot(
        opts["save-p4"].as<bool>()
    );

    ot.declareUserIntBranch("nfound_all",    0);
    ot.declareUserIntBranch("nfound_presel", 0);
    ot.declareUserIntBranch("nfound_sixb",   0);

    ////////////////////////////////////////////////////////////////////////
    // All pre-running configurations (corrections, methods from cfg, etc)
    ////////////////////////////////////////////////////////////////////////
  
    jsonLumiFilter jlf;
    if (is_data)
        jlf.loadJSON(config.readStringOpt("data::lumimask")); // just read the info for data, so if I just skim MC I'm not forced to parse a JSON


    SixB_functions sbf;
    
    JetTools jt;

    string jes_shift = opts["jes-shift-syst"].as<string>();
    bool do_jes_shift = (jes_shift != "nominal");
    cout << "[INFO] ... shifting jet energy scale? " << std::boolalpha << do_jes_shift << std::noboolalpha << endl;
    bool dir_jes_shift_is_up;
    if (do_jes_shift){
        string JECFileName = config.readStringOpt("parameters::JECFileName");
        auto tokens = split_by_delimiter(opts["jes-shift-syst"].as<string>(), ":");
        if (tokens.size() != 2)
            throw std::runtime_error(string("Cannot parse the jes shift name : ") + opts["jes-shift-syst"].as<string>());
        string jes_syst_name = tokens.at(0);
        dir_jes_shift_is_up   = (tokens.at(1) == "up"   ? true  :
                               tokens.at(1) == "down" ? false :
                               throw std::runtime_error(string("Could not parse jes direction ") + tokens.at(1)));
        cout << "       ... jec file name           : " << JECFileName << endl;
        cout << "       ... jet energy scale syst   : " << jes_syst_name << endl;
        cout << "       ... jet energy scale is up? : " << std::boolalpha << dir_jes_shift_is_up << std::noboolalpha << endl;
        jt.init_jec_shift(JECFileName, jes_syst_name);
    }

    string JERScaleFactorFile = config.readStringOpt("parameters::JERScaleFactorFile");
    string JERResolutionFile  = config.readStringOpt("parameters::JERResolutionFile");
    const int rndm_seed = opts["seed"].as<int>();
    cout << "[INFO] ... initialising JER corrector with the following parameters" << endl;
    cout << "       ... SF file         : " << JERScaleFactorFile << endl;
    cout << "       ... resolution file : " << JERResolutionFile << endl;
    cout << "       ... rndm seed       : " << rndm_seed << endl;
    jt.init_smear(JERScaleFactorFile, JERResolutionFile, rndm_seed);

    cout << "[INFO] ... jet resolution syst is    : " << opts["jer-shift-syst"].as<string>() << endl;
    cout << "[INFO] ... b regr resolution syst is : " << opts["bjer-shift-syst"].as<string>() << endl;
    const Variation jer_var  = string_to_jer_variation(opts["jer-shift-syst"].as<string>());
    const Variation bjer_var = string_to_jer_variation(opts["bjer-shift-syst"].as<string>());

    ////////////////////////////////////////////////////////////////////////
    // Execute event loop
    ////////////////////////////////////////////////////////////////////////

    int maxEvts = opts["maxEvts"].as<int>();
    if (maxEvts >= 0)
        cout << "[INFO] ... running on : " << maxEvts << " events" << endl;

    for (int iEv = 0; true; ++iEv)
    {
        if (maxEvts >= 0 && iEv >= maxEvts)
            break;

        if (!nat.Next()) break;
        if (iEv % 10000 == 0) cout << "... processing event " << iEv << endl;

        if (is_data && !jlf.isValid(*nat.run, *nat.luminosityBlock)){
            continue; // not a valid lumi
        }

        EventInfo ei;
        ot.clear();

        // global event info
        sbf.copy_event_info(nat, ei);

        // signal-specific gen info
        if (is_signal){
            sbf.select_gen_particles   (nat, ei);
            sbf.match_genbs_to_genjets (nat, ei);
            sbf.match_genbs_genjets_to_reco (nat, ei);
        }

        // // jet selections
        std::vector<Jet> all_jets    = sbf.get_all_jets     (nat);
        int nfound_all = sbf.n_gjmatched_in_jetcoll(nat, ei, all_jets);
        if (!is_data){
            if (do_jes_shift)
                all_jets = jt.jec_shift_jets(nat, all_jets, dir_jes_shift_is_up);
            all_jets = jt.smear_jets(nat, all_jets, jer_var, bjer_var);
        }
        std::vector<Jet> presel_jets = sbf.preselect_jets   (nat, all_jets);
        int nfound_presel = sbf.n_gjmatched_in_jetcoll(nat, ei, presel_jets);

        std::vector<Jet> sixb_jets   = sbf.select_sixb_jets (nat, presel_jets);
        int nfound_sixb = sbf.n_gjmatched_in_jetcoll(nat, ei, sixb_jets);

        // if (sixb_jets.size() < 6)
        //     continue;
        // sbf.pair_jets(nat, ei, sixb_jets);

        ot.userInt("nfound_all")    = nfound_all;
        ot.userInt("nfound_presel") = nfound_presel;
        ot.userInt("nfound_sixb")   = nfound_sixb;

        su::fill_output_tree(ot, nat, ei);
    }

    outputFile.cd();
    ot.write();
}