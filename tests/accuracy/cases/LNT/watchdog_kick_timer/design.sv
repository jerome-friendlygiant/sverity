// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned Timeout   = 1000,
  parameter int unsigned StartLoad = Timeout / 2,
  localparam int unsigned CntW     = $clog2(Timeout + 1)
) (
  input  logic clk_i,
  input  logic arst_i,
  input  logic rst_ni,
  input  logic kick_i,
  input  logic enable_i,
  output logic expired_o,
  output logic armed_o
);

  localparam logic [CntW-1:0] ResetCount = CntW'(StartLoad);

  logic [CntW-1:0] cnt_q;
  logic            armed_q;
  logic            expired_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      cnt_q <= ResetCount;
    end else if (kick_i) begin
      cnt_q <= CntW'(Timeout);
    end else if (armed_q && cnt_q != '0) begin
      cnt_q <= cnt_q - 1'b1;
    end
  end

  always_ff @(posedge clk_i or posedge arst_i) begin
    if (arst_i) begin
      armed_q   <= 1'b0;
      expired_q <= 1'b0;
    end else begin
      armed_q   <= enable_i;
      expired_q <= armed_q && (cnt_q == '0);
    end
  end

  assign expired_o = expired_q;
  assign armed_o   = armed_q;

endmodule
