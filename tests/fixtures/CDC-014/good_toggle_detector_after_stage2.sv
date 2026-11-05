// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-014 WARNING
// Near miss of mut_no_edge_detector.sv: the destination edge detector is restored as s2 AND NOT s3, a non-first stage combined with the stage after it. The chain is extended to three stages so that stage 3 exists.
module top(input logic clka, clkb, rst_n, pulse, output logic ev);
  logic tgl, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) tgl <= 1'b0; else if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s3, s2, s1} <= '0; else {s3, s2, s1} <= {s2, s1, tgl};
  assign ev = s2 & ~s3;
endmodule
