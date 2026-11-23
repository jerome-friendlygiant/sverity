// sverity-args: --top top
// expect: CST-005 WARNING
// expect-unresolved: CST-005
// expect-count: CST-005 WARNING 1
// expect-coverage: blackboxes INCOMPLETE
// revert: black_box = 1 => black_box = 0
// revert-expect-none: CST-005
(* black_box = 1 *) module opaque_leaf(input logic a, output logic b);
  assign b = ~a;
endmodule
module top(input logic d, output logic q);
  wire intermediate;
  opaque_leaf u_leaf(.a(d), .b(intermediate));
  assign q = intermediate;
endmodule
