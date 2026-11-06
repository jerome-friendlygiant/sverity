module top (
  input logic ref_clk,
  input logic reset_n,
  output logic [7:0] slow_count
);
  logic [1:0] phase;
  wire divided_clk;
  always_ff @(posedge ref_clk or negedge reset_n) begin
    if (!reset_n)
      phase <= '0;
    else
      phase <= phase + 1'b1;
  end
  assign divided_clk = phase[1];
  always_ff @(posedge divided_clk or negedge reset_n) begin
    if (!reset_n)
      slow_count <= '0;
    else
      slow_count <= slow_count + 1'b1;
  end
endmodule
