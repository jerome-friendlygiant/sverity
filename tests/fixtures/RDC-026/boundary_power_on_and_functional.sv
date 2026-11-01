// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two asynchronous controls that force the same value (power-on and functional reset): INFO, scenarios are not modelled.
// expect: RDC-026 INFO
// expect-none: RDC-026 WARNING
module top(input logic clka, por_n, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge por_n or negedge rst_n) if (!por_n || !rst_n) q <= 1'b0; else q <= d;
endmodule
