// sverity-args: --top top --sdc @DIR@/output_async.sdc
// The primary input's source clock is declared asynchronous to the output capture clock.
// expect: CDC-029 ERROR
module top(input logic src_clk, dst_clk, input logic flag_async, output logic flag_out);
  assign flag_out = flag_async;
endmodule
