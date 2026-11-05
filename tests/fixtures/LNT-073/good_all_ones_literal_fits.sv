// sverity-args: --top top
// Near-miss of bad_narrowing: -1 into a 4-bit target keeps its all-ones pattern under either signedness (exempt).
// expect-none: LNT-073
module top(output logic [3:0] lo);
  assign lo = -1;
endmodule
