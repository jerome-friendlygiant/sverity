// Adapted from lowRISC/common_cells src/cc_onehot_to_bin.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
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

module top #(
  parameter int unsigned OnehotWidth = 8,
  localparam int unsigned BinWidth = OnehotWidth == 1 ? 1 : $clog2(OnehotWidth)
) (
  input  logic [OnehotWidth-1:0] onehot_i,
  output logic [BinWidth-1:0] bin_o
);
  for (genvar bit_no = 0; bit_no < BinWidth; bit_no++) begin : gen_output_bit
    logic [OnehotWidth-1:0] bit_mask;
    for (genvar input_no = 0; input_no < OnehotWidth; input_no++) begin : gen_input_bit
      logic [BinWidth-1:0] input_code;
      assign input_code = BinWidth'(input_no);
      assign bit_mask[input_no] = input_code[bit_no];
    end
    assign bin_o[bit_no] = |(bit_mask & onehot_i);
  end
endmodule
