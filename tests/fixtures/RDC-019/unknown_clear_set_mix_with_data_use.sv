// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// rst_n clears q and sets r (a clear/set mix of one net, which the rule does not evaluate) and also feeds data into y; the data use alone gives the one WARNING, and the mix adds no second finding.
// expect: RDC-019 WARNING
// expect-count: RDC-019 WARNING 1
module top(input logic clka, rst_n, d, output logic q, r, y);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b1; else r <= d;
  always_ff @(posedge clka) y <= d & rst_n;
endmodule
