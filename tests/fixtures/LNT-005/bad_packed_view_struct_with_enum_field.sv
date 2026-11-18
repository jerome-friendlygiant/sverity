// sverity-args: --top top
// Lookalike of good_packed_view_struct_elements: a struct field that is an enum takes raw bits outside the enum's values.
// revert: e_t k; => logic [3:0] k;
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 12
module top(input logic [15:0] raw, output logic y);
  typedef enum logic [3:0] {X, Y} e_t;
  typedef struct packed {
    e_t k; logic [3:0] v;
  } tagged_t;
  tagged_t [1:0] tags;
  assign tags = raw;
  assign y = tags[0].v[0];
endmodule
