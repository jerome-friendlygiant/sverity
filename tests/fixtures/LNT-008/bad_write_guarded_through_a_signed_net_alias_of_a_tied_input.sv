// sverity-args: --top top
// The signed net gate is a copy of the input en, which the only instance ties to 0: gate == 1 is never true, so the write is dead.
// expect: LNT-008 WARNING 5
module leaf(input logic clk, input logic signed [1:0] en, output logic q);
  logic r;
  wire signed [1:0] gate = en;
  always_ff @(posedge clk) if (gate == 1) r <= 1'b1;
  assign q = r;
endmodule
module top(input logic clk, output logic q);
  leaf u(.clk, .en(2'sd0), .q);
endmodule
