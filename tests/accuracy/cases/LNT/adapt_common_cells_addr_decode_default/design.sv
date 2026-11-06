// Adapted from lowRISC/common_cells src/cc_addr_decode.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2019 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
// Reduced and modified for the SVerity golden set.

// The configuration producer is intentionally outside this reduced design.
module address_map_stub (
  input  logic [7:0] addr_i,
  output logic [1:0] idx_o,
  output logic       valid_o
);
endmodule

module top (
  input  logic [7:0] addr_i,
  output logic [1:0] idx_o,
  output logic       valid_o
);
  address_map_stub u_map (.addr_i(addr_i), .idx_o(idx_o), .valid_o(valid_o));
endmodule
