// sverity-args: --top top --sdc @DIR@/valid_zero_port_delay.sdc
// expect-none: CST-018
module top(input logic clk, d, output logic q);
  logic stage;
  always_ff @(posedge clk) begin stage <= d; q <= stage; end
endmodule
