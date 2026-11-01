// sverity-args: --top top
// revert: IDLE: y = 1'b0; => IDLE: y = 1'b0; DONE: y = 1'b1;
// revert-expect-none: LNT-017
// An enumerator of the state type has no arm (slang case-enum mapped to LNT-017).
// expect: LNT-017 WARNING
module top(input logic [1:0] sel, output logic y);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st;
  assign st = st_t'(sel);
  always_comb begin
    y = 1'b0;
    case (st)
      IDLE: y = 1'b0;
      RUN: y = 1'b1;
    endcase
  end
endmodule
