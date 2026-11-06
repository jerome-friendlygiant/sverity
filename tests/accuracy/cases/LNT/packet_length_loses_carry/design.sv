module top(input logic [7:0] payload_bytes,
  output logic [7:0] packet_bytes);

  localparam logic [8:0] HEADER_BYTES = 9'd16;
  logic [8:0] total_bytes;
  // Packet byte count includes the header, including for payloads above 239.
  always_comb begin
    total_bytes = {1'b0, payload_bytes} + HEADER_BYTES;
    packet_bytes = total_bytes;
  end

endmodule
