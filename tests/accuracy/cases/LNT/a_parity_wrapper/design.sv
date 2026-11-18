// Byte-wide register with an even-parity bit computed on the stored value.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       load,
  input  logic [7:0] data,
  output logic [7:0] word,
  output logic       parity
);
  logic [7:0] word_q;
  logic       parity_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      word_q   <= 8'h00;
      parity_q <= 1'b0;
    end else if (load) begin
      word_q   <= data;
      parity_q <= ^data;
    end
  end

  logic stored_parity;
  assign stored_parity = ^word_q[8:1];

  assign word   = word_q;
  assign parity = parity_q ^ stored_parity ^ (^word_q);
endmodule
