// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.
// SPDX-License-Identifier: SHL-0.51
// Adapted from src/cc_stream_mux.sv. @e73baaec2ca665cd80c3c384e9258e35242b829c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [7:0] data_i [2],
  input  logic [1:0] valid_i,
  input  logic       sel_i,
  input  logic       ready_i,
  output logic [7:0] data_o,
  output logic       valid_o,
  output logic [1:0] ready_o
);
  always_comb begin
    valid_o = 1'b0;
    ready_o = 2'b00;
    if (valid_i[sel_i]) begin
      data_o = data_i[sel_i];
      valid_o = 1'b1;
      ready_o[sel_i] = ready_i;
    end
  end
endmodule
