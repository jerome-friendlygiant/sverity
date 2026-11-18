// Adapted from common_cells src/cc_popcount.sv
// (commit e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2018 ETH Zurich and University of Bologna.
// Licensed under the Solderpad Hardware License, Version 0.51 (SHL-0.51).

`default_nettype none

package flag_cfg_pkg;
  localparam int unsigned MaxFlags = 16;
endpackage

module top
  import flag_cfg_pkg::*;
#(
  parameter  int unsigned InputWidth    = 64,
  localparam int unsigned PopcountWidth = $clog2(InputWidth+1)
) (
  input  wire    [InputWidth-1:0]    data_i,
  output logic   [PopcountWidth-1:0] popcount_o,
  output logic                       over_limit_o
);

  always_comb begin
    popcount_o = 0;
    for (int i = 0; i < InputWidth; i++) begin
      popcount_o += data_i[i];
    end
  end

  assign over_limit_o = (popcount_o > PopcountWidth'(MaxFlags));

endmodule

`default_nettype wire
