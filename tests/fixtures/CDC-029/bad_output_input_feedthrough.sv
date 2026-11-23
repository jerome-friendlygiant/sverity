// sverity-args: --top top --sdc @DIR@/output.sdc
// The output is timed against clkb, but the primary input has no launch-clock contract or synchronizer.
// expect: CDC-029 WARNING
module top(input logic clkb, input logic flag_async, output logic flag_out);
  assign flag_out = flag_async;
endmodule
