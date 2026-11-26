// sverity-args: --top top --sdc @FIXTURES@/_common/three_async.sdc
// r1 = s2 & a resets clkb and clkc registers and reports only its worst domain; r = r1 & ~x resets another clkc register: (s2, clkc) must still be reported at r.
// expect: RDC-007 ERROR
// expect-count: RDC-007 ERROR 2
module top(input logic clka, clkb, clkc, rst_n, scanmode, scan_rst_n, a, x, d, output logic qa, qb, qc, q);
  logic s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  logic r1, r;
  assign r1 = s2 & a;
  always_ff @(posedge clkb or negedge r1) if (!r1) qb <= 1'b0; else qb <= d;
  always_ff @(posedge clkc or negedge r1) if (!r1) qc <= 1'b0; else qc <= d;
  assign r = r1 & ~x;
  always_ff @(posedge clkc or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
