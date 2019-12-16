class StRingBuffer
{
  public:
    StRingBuffer(int len);
    String addChar(char val);
    String getString();
    void clear();
  private:
    int length = 0;
    int pos = 0;
    String data = "";
};

StRingBuffer::StRingBuffer(int len)
{
  length = len;
  clear();
}

void StRingBuffer::clear()
{
  data = "";
  for (pos = 0; pos < length; pos++)
    data += " ";
  pos = 0;
}

String StRingBuffer::addChar(char val)
{
  if (!isPrintable(val))
    val = ' ';
  data.setCharAt(pos, val);
  pos = (++pos)%length;
  return getString();
}

String StRingBuffer::getString()
{
  if (pos > 0 && length > 0)
    return data.substring(pos) + data.substring(0, pos);
  else
    return data;
}
