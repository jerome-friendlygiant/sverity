// Ratio unit: quotient of two unsigned bytes with a saturated result for a
// zero denominator, plus a decimal scaling of the quotient.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] num,
  input  logic [7:0] den,
  output logic [7:0] quot_o,
  output logic [7:0] tens_o,
  output logic       div_by_zero
);
  logic [7:0] quot_c;

  always_comb begin
    if (den == 8'd0) quot_c = 8'hff;
    else             quot_c = num / den;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      quot_o      <= 8'd0;
      tens_o      <= 8'd0;
      div_by_zero <= 1'b0;
    end else begin
      quot_o      <= quot_c;
      tens_o      <= quot_c / 8'd10;
      div_by_zero <= (den == 8'd0);
    end
  end
endmodule
