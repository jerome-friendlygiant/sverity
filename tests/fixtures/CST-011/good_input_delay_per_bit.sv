// sverity-args: --top top --sdc @DIR@/per_bit.sdc
// Input delays name each bit of din: every captured bit has a declared launch clock.
// expect-none: CST-011
module top(input logic clk, input logic [1:0] din, output logic [1:0] dout);
  always_ff @(posedge clk) dout <= din;
endmodule
