// sverity-args: --top top --sdc @DIR@/output_bit_modes.sdc
// expect: CDC-029 ERROR
// expect-count: CDC-029 ERROR 1
module top(input logic src_clk, dst_clk, input logic [1:0] data, output logic [1:0] result);
  assign result = data;
endmodule
