// sverity-args: --top top --sdc @DIR@/output_bit_same_clock.sdc
// expect-none: CDC-029
module top(input logic dst_clk, input logic [1:0] data, output logic [1:0] result);
  assign result = data;
endmodule
