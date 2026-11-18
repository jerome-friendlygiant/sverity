// Bidirectional pad cell: drives the pad while oe is set, always samples it.
module io_cell (
  input  logic oe,
  input  logic dout,
  output logic din,
  inout  wire  pad
);
  assign pad = oe ? dout : 1'bz;
  assign din = pad;
endmodule

// Two GPIO lanes plus a debug lane, inputs sampled into a register.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [1:0] drive_val,
  input  logic [1:0] drive_en,
  input  logic       dbg_en,
  input  logic       dbg_val,
  inout  wire  [1:0] gpio,
  output logic [2:0] sample
);
  logic [1:0] din_w;
  logic       dbg_in;

  io_cell u_io0 (.oe(drive_en[0]), .dout(drive_val[0]), .din(din_w[0]), .pad(gpio[0]));
  io_cell u_io1 (.oe(drive_en[1]), .dout(drive_val[1]), .din(din_w[1]), .pad(gpio[1]));
  io_cell u_dbg (.oe(dbg_en), .dout(dbg_val), .din(dbg_in));

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) sample <= 3'b000;
    else        sample <= {dbg_in, din_w};
  end
endmodule
