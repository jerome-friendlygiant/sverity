module fifo_write_side #(
  parameter int PTR_W = 3
) (
  input logic wclk,
  input logic arst_n,
  input logic push,
  input logic [PTR_W-1:0] read_ptr_async,
  output logic [PTR_W-1:0] write_ptr,
  output logic full
);
  always_ff @(posedge wclk or negedge arst_n) begin
    if (!arst_n) begin
      write_ptr <= '0;
      full <= 1'b0;
    end else begin
      if (push && !full) write_ptr <= write_ptr + 1'b1;
      full <= (write_ptr + 1'b1 == read_ptr_async);
    end
  end
endmodule

module fifo_read_side #(
  parameter int PTR_W = 3
) (
  input logic rclk,
  input logic arst_n,
  input logic pop,
  output logic [PTR_W-1:0] read_ptr
);
  always_ff @(posedge rclk or negedge arst_n) begin
    if (!arst_n) read_ptr <= '0;
    else if (pop) read_ptr <= read_ptr + 1'b1;
  end
endmodule

module top (
  input logic wclk,
  input logic rclk,
  input logic arst_n,
  input logic push,
  input logic pop,
  output logic [2:0] write_ptr,
  output logic [2:0] read_ptr,
  output logic full
);
  fifo_read_side #(.PTR_W(3)) u_read (
    .rclk(rclk), .arst_n(arst_n), .pop(pop), .read_ptr(read_ptr)
  );
  fifo_write_side #(.PTR_W(3)) u_write (
    .wclk(wclk), .arst_n(arst_n), .push(push), .read_ptr_async(read_ptr),
    .write_ptr(write_ptr), .full(full)
  );
endmodule
