// sverity-args: --top top --sdc @DIR@/core_input_delay.sdc
// The input delay declares din launched by core: no coverage gap.
// expect-none: CST-011
module top(input logic clk, input logic [7:0] din, output logic [7:0] dout);
  logic [7:0] staged;
  always_ff @(posedge clk) begin
    staged <= din;
    dout <= staged;
  end
endmodule
