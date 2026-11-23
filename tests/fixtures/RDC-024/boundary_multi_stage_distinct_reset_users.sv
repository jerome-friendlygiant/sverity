// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A single synchroniser chain whose stage s2 resets clka and follower s3 resets clkc
// emits one RDC-024 warning aggregating all reset user domains across the chain.
// expect-count: RDC-024 WARNING 1
module top(input logic clka, clkb, clkc, rst_n, req, d1, d2, output logic qa, qc);
  logic r_a, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r_a <= 1'b1; else r_a <= req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s3, s2, s1} <= 3'b111; else {s3, s2, s1} <= {s2, s1, r_a};
  always_ff @(posedge clka or negedge s2) if (!s2) qa <= 1'b0; else qa <= d1;
  always_ff @(posedge clkc or negedge s3) if (!s3) qc <= 1'b0; else qc <= d2;
endmodule
