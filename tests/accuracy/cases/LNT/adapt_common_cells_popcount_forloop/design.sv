// Adapted from lowRISC/common_cells src/cc_popcount.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2013-2018 ETH Zurich, University of Bologna
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
// Reduced and modified for the SVerity golden set.

module top #(
  parameter int unsigned InputWidth = 7,
  localparam int unsigned CountWidth = $clog2(InputWidth + 1)
) (
  input  logic [InputWidth-1:0] data_i,
  output logic [CountWidth-1:0] count_o
);
  always_comb begin
    count_o = '0;
    for (int i = 0; i < InputWidth; i++) begin
      count_o += data_i[i];
    end
  end
endmodule
