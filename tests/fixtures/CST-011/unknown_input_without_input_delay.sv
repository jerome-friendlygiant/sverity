// sverity-args: --top top --sdc @DIR@/core_only.sdc
// Constraints are supplied, but nothing declares the clock that launches din: it is analysed as asynchronous.
// expect: CST-011 WARNING 7
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, input logic [7:0] din, output logic [7:0] dout);
  logic [7:0] staged;
  always_ff @(posedge clk) begin
    staged <= din;
    dout <= staged;
  end
endmodule
