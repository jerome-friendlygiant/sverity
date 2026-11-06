// Adapted from lowRISC/common_cells src/cc_ecc_decode.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2020 ETH Zurich and University of Bologna.
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
  parameter int unsigned DataWidth = 8,
  localparam int unsigned ParityWidth = $clog2(DataWidth + 1),
  localparam int unsigned CodeWidth = DataWidth + ParityWidth
) (
  input  logic [CodeWidth-1:0] code_i,
  output logic [ParityWidth-1:0] syndrome_o,
  output logic [DataWidth-1:0] data_o
);
  logic [CodeWidth-1:0] corrected;

  always_comb begin : calculate_syndrome
    syndrome_o = '0;
    for (int unsigned p = 0; p < ParityWidth; p++) begin
      for (int unsigned bit_idx = 0; bit_idx < CodeWidth; bit_idx++) begin
        if (|(unsigned'(2 ** p) & (bit_idx + 1)))
          syndrome_o[p] ^= code_i[bit_idx];
      end
    end
  end

  always_comb begin : extract_data
    automatic int unsigned out_idx;
    corrected = code_i;
    if ((|syndrome_o) && (syndrome_o <= CodeWidth))
      corrected[syndrome_o - 1'b1] = ~code_i[syndrome_o - 1'b1];
    data_o = '0;
    out_idx = 0;
    for (int unsigned pos = 1; pos <= CodeWidth; pos++) begin
      if ((2 ** $clog2(pos)) != pos) begin
        data_o[out_idx] = corrected[pos-1];
        out_idx++;
      end
    end
  end
endmodule
