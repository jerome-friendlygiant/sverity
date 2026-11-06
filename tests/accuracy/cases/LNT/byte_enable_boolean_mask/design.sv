module top #(
  parameter int LANES = 4
)(
  input logic clk,
  input logic rst_n,
  input logic [LANES-1:0] requested_bytes,
  input logic [LANES-1:0] available_bytes,
  input logic [LANES*8-1:0] write_data,
  output logic [LANES*8-1:0] stored_data
);
  logic [LANES-1:0] enabled_bytes;
  // Each requested, available byte lane updates independently.
  assign enabled_bytes = requested_bytes && available_bytes;
  for (genvar lane = 0; lane < LANES; lane++) begin : bytes_g
    always_ff @(posedge clk) begin
      if (!rst_n)
        stored_data[lane*8 +: 8] <= '0;
      else if (enabled_bytes[lane])
        stored_data[lane*8 +: 8] <= write_data[lane*8 +: 8];
    end
  end
endmodule
