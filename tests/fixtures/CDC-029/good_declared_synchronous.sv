// sverity-args: --top top --sdc @DIR@/inputs.sdc
// set_input_delay -clock clka declares din synchronous to the capturing clock: INFO, not a violation.
// expect: CDC-029 INFO
// expect-none: CDC-029 WARNING
module top(input logic clka, rst_n, din, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= din;
endmodule
