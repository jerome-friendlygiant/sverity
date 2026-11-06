// Adapted from lowRISC/common_cells src/cc_stream_filter.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic valid_i,
  input  logic ready_i,
  input  logic drop_i,
  output logic valid_o,
  output logic ready_o
);
  // Dropped beats are consumed locally and never reach the downstream port.
  assign valid_o = drop_i ? 1'b0 : valid_i;
  assign ready_o = drop_i ? 1'b1 : ready_i;
endmodule
