// sverity-args: --top top
// A constant part-select past the vector reads X (the LRM defines it): the finding is an ERROR at the select, and the rest of the design
// is still analysed (no elaboration failure).
// expect: LNT-006 ERROR 7
// expect-none: LNT-001
module top(input logic [7:0] q, output logic p);
  assign p = ^q[8:1];
endmodule
