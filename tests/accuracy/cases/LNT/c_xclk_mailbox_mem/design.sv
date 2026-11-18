// Mailbox memory shared by two processors. Each side posts messages from
// its own clock domain into an 8-entry slot array; the reader picks a slot.
module top (
  input  logic        clk_a,
  input  logic        clk_b,
  input  logic        post_a,
  input  logic [2:0]  slot_a,
  input  logic [15:0] msg_a,
  input  logic        post_b,
  input  logic [2:0]  slot_b,
  input  logic [15:0] msg_b,
  input  logic [2:0]  rd_slot,
  output logic [15:0] rd_msg
);
  logic [15:0] slots [0:7];

  always @(posedge clk_a) begin
    if (post_a) slots[slot_a] <= msg_a;
  end

  always @(posedge clk_b) begin
    if (post_b) slots[slot_b] <= msg_b;
  end

  assign rd_msg = slots[rd_slot];
endmodule
