module top (
  input  logic clk_i,
  input  logic por_ni,
  input  logic debug_reset_i,
  input  logic enable_i,
  output logic [7:0] value_o
);
  logic [7:0] value_q;
  logic       both_reset_ni;

  // POR is active-low, while the debug reset is an independent active-high source.
  assign both_reset_ni = por_ni & ~debug_reset_i;

  always_ff @(posedge clk_i or negedge por_ni or posedge debug_reset_i) begin
    if (!por_ni)
      value_q <= 8'h00;
    else if (debug_reset_i)
      value_q <= 8'hff;
    else if (enable_i)
      value_q <= value_q + 8'h01;
  end

  assign value_o = value_q;
endmodule
