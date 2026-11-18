// sverity-args: --top top
// An enumerator of a local enum takes the name of a package enumerator that the wildcard import makes visible.
// revert: StDone = 1'b1 => StEnd = 1'b1
// revert-expect-none: LNT-078
// expect: LNT-078 INFO 11
package p;
  typedef enum logic {StIdle = 1'b0, StDone = 1'b1} st_e;
endpackage
module top (input logic a, output logic y);
  import p::*;
  typedef enum logic {StBusy = 1'b0, StDone = 1'b1} local_e;
  local_e s;
  always_comb begin s = local_e'(a); y = (s == StDone); end
endmodule
