/*
 * Copyright 2018, Chanhee Park <parkchanhee@gmail.com> and Daehwan Kim <infphilo@gmail.com>
 *
 * This file is part of HISAT 2.
 *
 * HISAT 2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HISAT 2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HISAT 2.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __REPEAT_BUILDER_H__
#define __REPEAT_BUILDER_H__

#include <iostream>
#include <fstream>
#include <limits>
#include <map>
#include "assert_helpers.h"
#include "word_io.h"
#include "mem_ids.h"
#include "ref_coord.h"
#include "ref_read.h"
#include "ds.h"
#include "repeat.h"
#include "blockwise_sa.h"

//#define DEBUGLOG

using namespace std;

/**
 * Encapsulates repeat parameters.
 */
class RepeatParameter {
public:
    TIndexOffU seed_len;         // seed length
    TIndexOffU seed_count;       // seed count
    TIndexOffU seed_mm;          // maximum edit distance allowed during initial seed extension
    TIndexOffU repeat_count;     // repeat count
    TIndexOffU min_repeat_len;   // minimum repeat length
    TIndexOffU max_repeat_len;   // maximum repeat length
    TIndexOffU max_edit;         // maximum edit distance allowed
    bool       symmetric_extend; // extend symmetrically
};

// Dump
//
// to_string
static string to_string(int val)
{
	stringstream ss;
	ss << val;
	return ss.str();
}


struct Fragments {
	bool contain(TIndexOffU pos) {
		if (pos >= joinedOff && pos < (joinedOff + length)) {
			return true;
		}
		return false;
	}

    TIndexOffU joinedOff;       // index within joined text
	TIndexOffU length;

	int frag_id;
	int seq_id;
	TIndexOffU seqOff;          // index within sequence 
	bool first;
};

struct RepeatGroup {
	string seq;
    
    EList<RepeatCoord<TIndexOffU> > positions;

    Coord coord; 
	EList<Edit> edits; 
    EList<string> snpIDs;
    
    EList<RepeatGroup> alt_seq;
    size_t base_offset;

	void merge(const RepeatGroup& rg)
	{
        alt_seq.push_back(rg);
#if 0
		alt_seq.push_back(rg.seq);
		for (int i = 0; i < rg.alt_seq.size(); i++) {
			alt_seq.push_back(rg.alt_seq[i]);
		}

		for (int i = 0; i < rg.positions.size(); i++) {
			positions.push_back(rg.positions[i]);
		}
#endif
	}

    void merge(const RepeatGroup& rg, const EList<Edit>& ed, const Coord& coord)
    {
        merge(rg);
        alt_seq.back().edits = ed;
        alt_seq.back().coord = coord;
    }

	bool empty(void) 
	{ 
		return positions.size() == 0;
	}

	void set_empty(void) 
	{ 
		positions.clear();
        assert(positions.size() == 0);
	}

    void writeSNPs(ostream& fp, const string& rep_chr_name)
    {
        size_t ref_base = base_offset + coord.off();
        int rd_gaps = 0;    // Read Gaps
        int rf_gaps = 0;    // Ref Gaps

        for(size_t i = 0; i < edits.size(); i++) {
            Edit& ed = edits[i];

            assert_geq(edits[i].pos + rd_gaps - rf_gaps, 0);
            if(ed.isMismatch()) {

                fp << snpIDs[i];
                fp << "\t" << "single";
                fp << "\t" << rep_chr_name;
                fp << "\t" << ref_base + edits[i].pos + rd_gaps - rf_gaps;
                fp << "\t" << edits[i].qchr;
                fp << endl;
            } else if(ed.isReadGap()) {

                fp << snpIDs[i];
                fp << "\t" << "deletion";
                fp << "\t" << rep_chr_name;
                fp << "\t" << ref_base + edits[i].pos + rd_gaps - rf_gaps;
                fp << "\t" << 1;    // TODO
                fp << endl;

                rd_gaps++;
            } else if(ed.isRefGap()) {
                fp << snpIDs[i];
                fp << "\t" << "insertion";
                fp << "\t" << rep_chr_name;
                fp << "\t" << ref_base + edits[i].pos + rd_gaps - rf_gaps;
                fp << "\t" << (char)edits[i].qchr;  // TODO
                fp << endl;

                rf_gaps++;
            } else {
                assert(false);
            }
        }
    }

    void buildSNPs(size_t& base_idx)
    {
        snpIDs.clear();
        for(size_t i = 0; i < edits.size(); i++) {
            snpIDs.push_back("rps" + to_string(base_idx++));
        }
    }

    void writeHaploType(ofstream& fp, const string& rep_chr_name, size_t& base_idx)
    {
        assert_gt(edits.size(), 0);
        assert_eq(edits.size(), snpIDs.size());

        size_t ref_base = base_offset + coord.off();

        int rd_gaps = 0;
        int rf_gaps = 0;

        for(size_t i = 0; i < edits.size(); i++) {
            if(edits[i].isReadGap()) {
                rd_gaps++;
            } else if(edits[i].isRefGap()) {
                rf_gaps++;
            }
        }

        size_t left = edits[0].pos;
        size_t right = edits[edits.size() - 1].pos + rd_gaps - rf_gaps;

        fp << "rpht" << base_idx++;
        fp << "\t" << rep_chr_name;
        fp << "\t" << ref_base + left;
        fp << "\t" << ref_base + right;
        fp << "\t";
        for(size_t i = 0; i < edits.size(); i++) {
            if (i != 0) {
                fp << ",";
            }
            fp << snpIDs[i];
        }
        fp << endl;
    }

};

struct SeedExt {
    // seed extended position [first, second)
    pair<TIndexOffU, TIndexOffU> orig_pos;
    pair<TIndexOffU, TIndexOffU> pos;

    // extension bound. the seed must be placed on same fragment
    // [first, second)
    pair<TIndexOffU, TIndexOffU> bound;

    uint32_t ed;          // edit distance
    uint32_t total_ed;    // total edit distance
    bool done;            // done flag
    TIndexOffU baseoff;   // offset in consensus_merged
    
    TIndexOffU backbone;  // backbone seed number
    
    SeedExt() {
        reset();
    };

    void reset() {
        done = false;
        ed = total_ed = 0;
        orig_pos.first = 0;
        orig_pos.second = 0;
        pos.first = 0;
        pos.second = 0;
        bound.first = 0;
        bound.second = 0;
        baseoff = 0;
        backbone = 0;
    };
};

// find and write repeats
template<typename TStr>
class RepeatGenerator {

public:
	RepeatGenerator();
	RepeatGenerator(
                    TStr& s,
                    const EList<RefRecord>& szs,
                    EList<string>& ref_names,
                    bool forward_only,
                    BlockwiseSA<TStr>& sa,
                    const string& filename);
    ~RepeatGenerator();


public:
	void build(const RepeatParameter& rp);
	void buildNames();
	int mapJoinedOffToSeq(TIndexOffU joined_pos);
	int getGenomeCoord(TIndexOffU joined_pos, string& chr_name, TIndexOffU& pos_in_chr);

	void buildJoinedFragment();

	static bool compareRepeatGroupByJoinedOff(const RepeatGroup& a, const RepeatGroup& b)
	{
		return a.positions[0].joinedOff < b.positions[0].joinedOff;
	}
	void sortRepeatGroup();

    void saveRepeatPositions(ofstream& fp, RepeatGroup& rg);
	void saveFile();
	void saveRepeatSequence();
	void saveRepeatGroup();

    void addRepeatGroup(map<TIndexOffU, TIndexOffU>& seedpos_to_repeatgroup,
                        const string& rpt_seq,
                        const EList<RepeatCoord<TIndexOffU> >& positions);
    void mergeRepeatGroup();
    void groupRepeatGroup(TIndexOffU rpt_edit);
	void adjustRepeatGroup(bool flagGrouping = false);
    RepeatGroup* findRepeatGroup(const string&);

    TIndexOffU getEnd(TIndexOffU e);
    TIndexOffU getStart(TIndexOffU e);
	TIndexOffU getLCP(TIndexOffU a, TIndexOffU b);

	void repeat_masking();
    void init_dyn(const RepeatParameter& rp);

    bool checkSequenceMergeable(const string& ref,
                                const string& read,
                                EList<Edit>& edits,
                                Coord& coord,
                                TIndexOffU rpt_len,
                                TIndexOffU max_edit = 10);
    int alignStrings(const string&, const string&, EList<Edit>&, Coord&);
    void makePadString(const string&, const string&, string&, size_t);

    void seedExtension(string& seed_string,
                       EList<SeedExt>& seeds,
                       string& consensus_merged,
                       const RepeatParameter& rp);

    void saveSeedExtension(const string& seed_string,
                           const EList<SeedExt>& seeds,
                           const RepeatParameter& rp,
                           TIndexOffU rpt_grp_id,
                           ostream& fp,
                           const string& consensus_merged,
                           size_t& total_rep_seq_len);

    void seedGrouping(const RepeatParameter& rp);

    void doTest(const RepeatParameter& rp,
                const string& refstr,
                const string& readstr);
    void doTestCase1(const string&, const string&, TIndexOffU);
    
private:
    void get_consensus_seq(EList<SeedExt>& seeds,     // seeds
                           size_t sb,                 // seed begin
                           size_t se,                 // seed end
                           size_t min_left_ext,       
                           size_t min_right_ext,
                           size_t max_ed,             // maximum edit distance allowed
                           const RepeatParameter& rp,
                           EList<size_t>& ed_seed_nums,
                           EList<string>* left_consensus,
                           EList<string>* right_consensus);
    
    
private:
    const int output_width = 60;
    
    TStr& s_;
    const EList<RefRecord>& szs_;
    EList<string> ref_names_;
    EList<string>& ref_namelines_;
    bool forward_only_;
    string filename_;
    
    BlockwiseSA<TStr>& bsa_;
    
    // mapping info from joined string to genome
    EList<Fragments> fraglist_;
    
    //
    EList<RepeatGroup> rpt_grp_;
    
    TIndexOffU forward_length_;
    
    // Fragments Cache
#define CACHE_SIZE_JOINEDFRG    10
    Fragments cached_[CACHE_SIZE_JOINEDFRG];
    int num_cached_ = 0;
    int victim_ = 0;    /* round-robin */
    
    //
    SimpleFunc scoreMin_;
    SimpleFunc nCeil_;
    SimpleFunc penCanIntronLen_;
    SimpleFunc penNoncanIntronLen_;
    
    Scoring *sc_;
    SwAligner swa;
    LinkedEList<EList<Edit> > rawEdits_;
    RandomSource rnd_;
};

int strcmpPos(const string&, const string&, TIndexOffU&);
template<typename TStr> void dump_tstr(TStr& s);

#endif /* __REPEAT_BUILDER_H__ */
