module checksum_engine (
  input  logic [15:0] payload,
  input  logic        bypass,
  input  logic        invert,
  output logic [7:0]  checksum
);
  task automatic compute_checksum(
    input  logic [15:0] value,
    input  logic        skip,
    input  logic        flip,
    output logic [7:0]  result
  );
    if (!skip) begin
      result = value[7:0] ^ value[15:8];
      if (flip)
        result = ~result;
    end
  endtask

  always_comb begin
    compute_checksum(payload, bypass, invert, checksum);
  end
endmodule

module top (
  input  logic [15:0] payload,
  input  logic        bypass,
  input  logic        invert,
  output logic [7:0]  checksum
);
  checksum_engine u_engine (
    .payload(payload),
    .bypass(bypass),
    .invert(invert),
    .checksum(checksum)
  );
endmodule
