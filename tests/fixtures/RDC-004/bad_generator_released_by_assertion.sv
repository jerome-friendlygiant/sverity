// sverity-args: --top top --sdc @DIR@/generator.sdc
// revert: & ~dbg_sync_q[1]; => & dbg_sync_q[1];
// revert-expect-none: RDC-004 WARNING
// revert-expect-none: RDC-011 WARNING
// The inverted debug reset synchroniser output releases the generated reset when the debug reset asserts: the reset does not propagate.
// expect: RDC-004 WARNING
// expect: RDC-011 WARNING
module top (
  input  logic clk,
  input  logic por_ni,
  input  logic dbg_rst_ni,
  output logic acc_rst_no
);
  logic [1:0] por_sync_q;
  logic [1:0] dbg_sync_q;
  logic       acc_rst_q;

  always_ff @(posedge clk or negedge por_ni)
    if (!por_ni) por_sync_q <= 2'b00;
    else         por_sync_q <= {por_sync_q[0], 1'b1};

  always_ff @(posedge clk or negedge dbg_rst_ni)
    if (!dbg_rst_ni) dbg_sync_q <= 2'b00;
    else             dbg_sync_q <= {dbg_sync_q[0], 1'b1};

  always_ff @(posedge clk or negedge por_ni)
    if (!por_ni) acc_rst_q <= 1'b0;
    else         acc_rst_q <= por_sync_q[1] & ~dbg_sync_q[1];

  assign acc_rst_no = acc_rst_q;
endmodule
