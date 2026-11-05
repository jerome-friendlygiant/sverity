// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_toggle_sync.sv: logic is inserted between the toggle register and its first synchroniser stage.
// expect: CDC-014 WARNING
// revert: {s3, s2, s1} <= {s2, s1, tgl & pulse}; => {s3, s2, s1} <= {s2, s1, tgl};
// revert-expect-none: CDC-014 WARNING
module top(input logic clka, clkb, rst_n, pulse, output logic ev);
  logic tgl, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) tgl <= 1'b0; else if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s3, s2, s1} <= '0; else {s3, s2, s1} <= {s2, s1, tgl & pulse};
  assign ev = s2 ^ s3;
endmodule
