module top (
  input logic clk,
  input logic reset_n,
  input logic [7:0] bank_a,
  input logic [7:0] bank_b,
  output logic [7:0] output_q
);
  logic bank_sel;
  logic [7:0] selected;
  always_ff @(posedge clk) begin
    if (!reset_n)
      bank_sel <= 1'b0;
    else
      bank_sel <= ~bank_sel;
  end
  assign selected = bank_sel ? bank_b : bank_a;
  always_ff @(posedge clk)
    output_q <= selected;
endmodule
