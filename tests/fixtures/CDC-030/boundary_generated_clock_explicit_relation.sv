// sverity-args: --top top --sdc @DIR@/boundary.sdc
// Even with an explicit master/generated clock relationship, phase alignment is assumed rather than derived.
// expect: CDC-030 WARNING
// expect-confidence: CDC-030 unresolved
module top(input logic clka, rst_n, d, output logic q);
  logic div;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) div <= 1'b0; else div <= ~div;
  always_ff @(posedge div or negedge rst_n)
    if (!rst_n) q <= 1'b0; else q <= d;
endmodule
